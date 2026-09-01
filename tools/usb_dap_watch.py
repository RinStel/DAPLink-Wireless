# SPDX-License-Identifier: GPL-3.0-or-later
"""刚连接窗口期的 DAP 取证监控：分别记录 EP0、DAP bulk 与主机侧旁观者证据。

设计约束（决定了为什么必须这样采样）：

* 固件的 vendor 命令 0x80/0x81 是 DAP bulk 命令（`cmsis_dap.c`），不是 USB
  控制请求，所以 DAP 停摆期间读不到任何固件内部状态——取证只能靠 PC 侧独立
  旁观者：EP0 控制传输、MSC 卷、CDC 端口、Windows PnP 节点状态。
* 三种候选根因的可见差异必须被区分：固件 disconnect/复位（整机节点消失、
  MSC/CDC 同跳）、WinUSB 绑定间隙（节点在但 libusb 打不开该接口）、主循环
  未服务 DAP（EP0/MSC/CDC 全正常，只有 bulk 超时）。
* 采样必须早于故障发生，因此在插入或复位板子之前启动，并忽略首次成功之前
  的失败（那时设备本来就还没出现）。

用法：

    python tools/usb_dap_watch.py --seconds 180
    python tools/usb_dap_watch.py --seconds 120 --interval 0.1 --diag 2.0

只发送 DAP_Info(0xFF)，不触碰 SWD/SwjPins/Transfer，因此对被调试目标无副作用。
"""
import argparse
import datetime
import json
import os
import struct
import subprocess
import sys
import threading
import time

import libusb_package
import serial
import usb.core
import usb.util
from serial.tools import list_ports

VID = 0x28E9
PID = 0x1290
DAP_IF = 3
DAP_PACKET_SIZE = 64
CMD_INFO = 0x00
CMD_DIAG_PAGE = 0x81
INFO_PROTOCOL_VERSION = 0xFF
DAP_ERROR = 0xFF

# 一次 PowerShell 调用同时取 PnP 节点与本机 MSC 磁盘节点，避免为每个旁观者起一个进程。
# 必须按完整 VID&PID 匹配：只查 "28E9" 会误命中无关设备的地址哈希（如 9&28E90DC8）。
# MSC 在磁盘层露出的节点是 USBSTOR\...（含设备序列号），不是 USB\VID_...，因此另走 Model。
HOST_SNAPSHOT_SCRIPT = r"""
$pattern = 'VID_28E9&PID_1290'
# 必须用 -PresentOnly：Win32_PnPEntity 会列出已断开但缓存着的节点，拿它当
# “设备在位”会恒为真（曾因此把“插入后 1.0s 就有 4 个节点”当成目击证据）。
# Problem 枚举转 int：0=正常，与 CM_PROB_* 编号一致。
Get-PnpDevice -PresentOnly |
    Where-Object { $_.InstanceId -match $pattern } |
    ForEach-Object { 'pnp|{0}|{1}|{2}' -f $_.InstanceId, $_.Status,
        [int]$_.Problem }
Get-CimInstance Win32_DiskDrive |
    Where-Object { $_.PnPDeviceID -match $pattern -or $_.Model -match 'DAPLINK' } |
    ForEach-Object { 'disk|{0}|{1}' -f $_.Status, $_.Model }
"""


def stamp():
    """墙钟时间戳：没有它就无法把采样和 Windows 事件日志/setupapi 时间线对齐。"""
    return datetime.datetime.now().isoformat(timespec="milliseconds")


def describe_error(exc):
    """把 pyusb/WinUSB 异常压成可 grep 的短标签，保留 errno 供事后判读。"""
    errno = getattr(exc, "errno", None)
    strerror = getattr(exc, "strerror", None) or str(exc)
    return "errno=%s %s" % (errno, strerror.strip().splitlines()[0])


def parse_host_snapshot(text, com_ports):
    """解析快照输出，并把 PnP 状态归一成单一判据字段。

    ConfigManagerErrorCode 是 CM_PROB_* 编号（0=正常，10=启动失败，
    43=有故障…），比 Get-PnpDevice 的文本状态更适合做时间序列比对。
    """
    instances = []
    problems = []
    msc_disks = []
    for line in text.splitlines():
        fields = line.strip().split("|")
        if fields[0] == "pnp" and len(fields) >= 4:
            instances.append(fields[1])
            code = (fields[3] or "0").strip()
            if fields[2].upper() != "OK" or code != "0":
                problems.append("%s:%s/%s" % (fields[1], fields[2], code))
        elif fields[0] == "disk" and len(fields) >= 2:
            # 只取 Status+Model 做判据：盘符和 PHYSICALDRIVE 序号在重枚举后可能变，
            # 拿它们做集合会造出假跳变。
            msc_disks.append("|".join(fields[1:3]))
    if not instances:
        state = "absent"
    elif problems:
        state = "problem"
    else:
        state = "ok"
    return {
        "pnp_state": state,
        "pnp_problems": problems,
        "pnp_instances": sorted(instances),
        "msc_disks": sorted(msc_disks),
        "com_ports": sorted(com_ports),
    }


def list_com_ports():
    return [port.device for port in list_ports.comports()
            if port.vid == VID and port.pid == PID]


def probe_cdc(com_ports, timeout_s=0.25):
    """打开 CDC 端口作为“固件是否还活着”的独立目击。

    打开一个 CDC-ACM 端口会迫使 usbser 经由 EP0 发 SET_LINE_CODING，设备不应答
    就打不开——这跟 DAP bulk 和 MSC 都是独立通道。被其他终端占用时记 busy，
    不能当作设备无应答。
    """
    results = []
    for port in com_ports:
        handle = None
        try:
            handle = serial.Serial(port, 115200, timeout=timeout_s)
            results.append("%s:OK" % port)
        except Exception as error:
            text = describe_error(error)
            if "access" in text.lower() or "denied" in text.lower() or \
                    "busy" in text.lower():
                results.append("%s:BUSY" % port)
            else:
                results.append("%s:FAIL(%s)" % (port, text[:48]))
        finally:
            if handle is not None:
                try:
                    handle.close()
                except Exception:
                    pass
    return results


def cdc_alive(results):
    """本轮快照里是否至少有一个 CDC 端口真正打开成功（BUSY 不算）。"""
    return any(item.endswith(":OK") for item in results or [])


def collect_host_snapshot(pnp_timeout):
    """取一次主机侧快照；失败时返回带 error 的快照而不是抛出。"""
    snapshot = {"pnp_state": "unknown", "pnp_problems": [], "pnp_instances": [],
                "msc_disks": [], "com_ports": [], "cdc_open": [], "error": None}
    try:
        completed = subprocess.run(
            ["pwsh", "-NoProfile", "-Command", HOST_SNAPSHOT_SCRIPT],
            capture_output=True, text=True, timeout=pnp_timeout, check=False)
        if completed.returncode != 0:
            snapshot["error"] = "pwsh rc=%d" % completed.returncode
            return snapshot
    except (OSError, subprocess.SubprocessError) as error:
        snapshot["error"] = describe_error(error)
        return snapshot
    snapshot.update(parse_host_snapshot(completed.stdout, list_com_ports()))
    snapshot["cdc_open"] = probe_cdc(snapshot["com_ports"])
    return snapshot


class HostSampler(threading.Thread):
    """后台低频取主机侧证据，避免污染 DAP 往返延迟测量。"""

    def __init__(self, started, interval):
        super().__init__(daemon=True)
        self.started = started
        self.interval = interval
        self.snapshots = []
        self.lock = threading.Lock()
        self.stop_event = threading.Event()

    def run(self):
        while not self.stop_event.is_set():
            began = time.monotonic()
            snapshot = collect_host_snapshot(20.0)
            snapshot["sensor"] = "host"
            snapshot["t"] = began - self.started
            snapshot["wall"] = stamp()
            with self.lock:
                self.snapshots.append(snapshot)
            remaining = self.interval - (time.monotonic() - began)
            if remaining > 0:
                self.stop_event.wait(remaining)

    def take(self):
        with self.lock:
            return list(self.snapshots)


def host_evidence(snapshots, start, end):
    """返回故障窗口内（含前后各一次快照）的旁观者变化判据。

    区分两件相反的事：“窗口前存在、窗口内消失”是整机重枚举；“窗口前从未就绪、
    窗口内才出现”是功能晚启动。拿集合差异一律当“跳变”会把后者误判成前者
    （实际栽过一次：插入后连续 60s 打不开句柄、磁盘一直没建对象，被报成
    device-dropout）。
    """
    window = [item for item in snapshots
              if start - 2.0 <= item["t"] <= end + 2.0]
    usable = [item for item in snapshots if item["pnp_state"] != "unknown"]
    evidence = {"host_snapshots": len(window), "pnp_state": "unknown",
                "pnp_churn": False, "pnp_vanished": False,
                "disk_lost": False, "com_lost": False,
                "disk_late": False, "cdc_alive": False,
                "host_error": False}
    if not window:
        return evidence
    evidence["cdc_alive"] = any(cdc_alive(item.get("cdc_open", []))
                                for item in window)

    def vanished(key):
        """窗口之前有、窗口之内没了：真消失，需要设备在位的基线才成立。"""
        past = [item for item in usable if item["pnp_instances"] and
                item["t"] < start]
        if not any(item[key] for item in past):
            return False
        return any(not item[key] for item in usable
                   if start - 2.0 <= item["t"] <= end + 2.0)

    evidence["disk_lost"] = vanished("msc_disks")
    evidence["com_lost"] = vanished("com_ports")
    evidence["pnp_vanished"] = vanished("pnp_instances")
    instance_sets = {tuple(item["pnp_instances"]) for item in window
                     if item["pnp_state"] != "unknown"}
    evidence["pnp_churn"] = len(instance_sets) > 1
    # 窗口前没有“设备在位”的基线，而磁盘在窗口内还不存在、窗口之后才出现：
    # 晚启动。这里必须看窗口之后的快照，“从没有过”和“迟到了”的差别就在这里。
    arrived = [item for item in usable if item["pnp_instances"] and
               item["t"] < start]
    if not arrived:
        if any(not item["msc_disks"] for item in window) and \
                any(item["msc_disks"] for item in usable if item["t"] > end):
            evidence["disk_late"] = True
    # 绑定间隙是瞬态的，窗口结束时的状态已恢复，因此取窗口内最差状态。
    for state in ("absent", "problem", "unknown", "ok"):
        if any(item["pnp_state"] == state for item in window):
            evidence["pnp_state"] = state
            break
    evidence["host_error"] = any(item["error"] for item in window)
    return evidence


def diagnose(device_gone, attach_failed, ep0_failed, host):
    """按证据强度分诊：整机掉线 > 主机拿不到句柄 > 固件不应答 > 仅接口停摆。

    判“掉线”必须有“在位过又消失”的基线：插入前节点本来就不存在，拿窗口内
    出现 absent / 节点集合在变大 当掉线证据，会把“功能晚启动”误读成“重枚举风暴”。
    """
    if device_gone or host["disk_lost"] or host["pnp_vanished"]:
        return "device-dropout(整机重枚举：固件 disconnect/复位或拔插)"
    if host.get("cdc_alive"):
        return ("function-gap(窗口内 CDC 能打开：固件在 EP0 上应答，卡住的是 "
                "MSC/WinUSB 功能启动，不是整机失联)")
    if attach_failed:
        return ("host-attach-gap(设备已枚举但主机拿不到设备句柄；若同时有"
                "MSC 功能晚就绪，则是设备栈未启动，而非驱动安装慢)")
    if ep0_failed:
        return "ep0-stall(句柄可用但控制传输无应答)"
    if host["disk_late"]:
        return "late-function-start(设备在总线且可附着，但 MSC/DAP 功能晚就绪)"
    if host["pnp_state"] == "problem":
        return "host-binding-gap(节点报 CM_PROB 错误码)"
    if host["com_lost"]:
        return "composite-function-gap(CDC 功能跳、DAP 不答)"
    return "interface-only-stall(EP0/MSC/CDC 正常，仅 DAP bulk 无应答)"


def arrival_time(samples, host_snapshots):
    """设备第一次出现在主机视野里的时刻。

    之前的失败是“还没插”，不是故障。必须用“出现”而不是“首次成功”做分界：
    故障本身就长在刚插入那一段，拿首次成功做分界会把整段窗口当作
    pre-arrival 吐掉（实际栽过一次：插入后 60.6s 不可用被报成“无故障窗口”）。
    """
    moments = [item["t"] for item in samples if item.get("device_present")]
    moments += [item["t"] for item in host_snapshots
                if item["pnp_state"] not in ("absent", "unknown")]
    return min(moments) if moments else None


def classify_stalls(samples, host_snapshots, injections=(), arrival=None):
    """把连续失败的 DAP 采样合并成故障窗口，并附分诊结论。

    arrival 之前的失败不计（设备尚未插入）；默认自动从采样与快照推导。
    """
    if arrival is None:
        arrival = arrival_time(samples, host_snapshots)
    events = []
    run = None
    for sample in samples:
        if arrival is not None and sample["t"] < arrival:
            continue
        if sample["dap_ok"]:
            if run is not None:
                events.append(close_run(run, host_snapshots, injections))
                run = None
            continue
        if run is None:
            run = {"start": sample["t"], "stages": [], "errors": []}
        run["end"] = sample["t"]
        run["stages"].append(sample.get("stage"))
        if sample.get("error"):
            run["errors"].append(sample["error"])
        run["device_gone"] = run.get("device_gone", False) or \
            not sample["device_present"]
        run["attach_failed"] = run.get("attach_failed", False) or \
            not sample.get("attach_ok", True)
        run["ep0_failed"] = run.get("ep0_failed", False) or \
            (sample["device_present"] and not sample["ep0_ok"])
    if run is not None:
        run["truncated"] = True
        events.append(close_run(run, host_snapshots, injections))
    return events


def close_run(run, host_snapshots, injections=()):
    host = host_evidence(host_snapshots, run["start"], run["end"])
    stage_counts = {}
    for stage in run["stages"]:
        stage_counts[stage] = stage_counts.get(stage, 0) + 1
    event = {
        "kind": "dap-stall",
        "start_s": round(run["start"], 3),
        "end_s": round(run.get("end", run["start"]), 3),
        "duration_s": round(run.get("end", run["start"]) - run["start"], 3),
        "samples": len(run["stages"]),
        "truncated": run.get("truncated", False),
        "stages": stage_counts,
        "device_gone": run.get("device_gone", False),
        "attach_failed": run.get("attach_failed", False),
        "ep0_failed": run.get("ep0_failed", False),
        "sample_errors": run["errors"][:3],
        "spans_injection": any(
            run["start"] <= moment <= run.get("end", run["start"])
            for moment in injections),
        "host": host,
    }
    event["diagnosis"] = diagnose(
        event["device_gone"], event["attach_failed"], event["ep0_failed"], host)
    return event


def render_summary(samples, events, host_snapshots, arrival=None):
    ok = [item for item in samples if item["dap_ok"]]
    lines = ["采样 %d 次，DAP 可用 %d 次" % (len(samples), len(ok))]
    if arrival is not None:
        lines.append("设备首次出现于 %.2fs（之前的失败是还没插，不计故障）" % arrival)
    if host_snapshots:
        last = host_snapshots[-1]
        lines.append("主机侧快照 %d 次（末次）：PnP %s｜MSC 磁盘 %d｜CDC %s%s" % (
            len(host_snapshots), last["pnp_state"], len(last["msc_disks"]),
            ",".join(last["com_ports"]) or "无",
            "｜快照错误: %s" % last["error"] if last["error"] else ""))
    else:
        lines.append("未采集主机侧快照（--no-host），分诊只能依赖 EP0 与在位判据")
    if ok:
        latencies = sorted(item["latency_us"] for item in ok
                           if item["latency_us"] is not None)
        if latencies:
            lines.append("DAP 附着+往返 p50=%.0fus p99=%.0fus max=%.0fus"
                         "（含每次 open/claim 开销，不能与裸 bench 对比）" % (
                             latencies[len(latencies) // 2],
                             latencies[min(len(latencies) - 1,
                                           int(len(latencies) * 0.99))],
                             latencies[-1]))
    restarts = [item for item in samples if item.get("session_restarted")]
    if restarts:
        lines.append("固件重启迹象 %d 次（诊断 session_us 回退）：%s" % (
            len(restarts), [round(item["t"], 2) for item in restarts]))
    versions = sorted({item["bcd_device"] for item in samples
                       if item.get("bcd_device")})
    lines.append("观察到的 bcdDevice：%s%s" % (
        ", ".join(versions) if versions else "无（全程未发现设备）",
        "；多个值说明采样期间镜像变了" if len(versions) > 1 else ""))
    if not events:
        lines.append("未观察到 DAP 故障窗口——请把采样间隔调密或确认复现时段被覆盖")
    for index, event in enumerate(events, 1):
        lines.append("")
        lines.append("窗口 %d: %.2fs→%.2fs（%.2fs，%d 次采样%s）" % (
            index, event["start_s"], event["end_s"], event["duration_s"],
            event["samples"], "，被提前中断" if event["truncated"] else ""))
        lines.append("  失败阶段: %s" % (event["stages"] or {}))
        lines.append("  设备在位丢失: %s｜打不开句柄: %s｜EP0 无应答: %s｜PnP: %s%s" % (
            event["device_gone"], event["attach_failed"],
            event["ep0_failed"], event["host"]["pnp_state"],
            "（节点集合有变动）" if event["host"]["pnp_churn"] else ""))
        lines.append("  MSC 磁盘消失: %s｜晚启动: %s｜CDC 端口消失: %s｜窗口内 CDC 可打开: %s" % (
            event["host"]["disk_lost"], event["host"]["disk_late"],
            event["host"]["com_lost"], event["host"]["cdc_alive"]))
        if event["sample_errors"]:
            lines.append("  首个错误: %s" % event["sample_errors"][0])
        lines.append("  分诊: %s" % event["diagnosis"])
        if event["spans_injection"]:
            lines.append("  注：本窗口内含主动注入（--inject），是实验而非自然复现")
    return "\n".join(lines)


class DapWatcher:
    def __init__(self, interval, seconds, timeout_ms, diag_interval, out_path,
                 with_host, inject=None, inject_at=2.0):
        self.backend = libusb_package.get_libusb1_backend()
        self.interval = interval
        self.seconds = seconds
        self.timeout_ms = timeout_ms
        self.diag_interval = diag_interval
        self.out_path = out_path
        self.with_host = with_host
        self.inject = inject
        self.inject_at = inject_at
        self.injected = False
        self.injections = []
        self.started = time.monotonic()
        self.samples = []
        self.endpoints = None
        self.drain_pending = False
        self.last_diag = None
        self.last_session_us = None
        self.last_dap_ok = None

    def open_log(self):
        if self.out_path is None:
            return None
        directory = os.path.dirname(os.path.abspath(self.out_path))
        os.makedirs(directory, exist_ok=True)
        return open(self.out_path, "w", encoding="utf-8")

    def resolve_endpoints(self, device):
        if self.endpoints is not None:
            return self.endpoints
        interface = device.get_active_configuration()[(DAP_IF, 0)]
        out = usb.util.find_descriptor(
            interface, custom_match=lambda e:
            usb.util.endpoint_direction(e.bEndpointAddress) ==
            usb.util.ENDPOINT_OUT)
        in_ = usb.util.find_descriptor(
            interface, custom_match=lambda e:
            usb.util.endpoint_direction(e.bEndpointAddress) ==
            usb.util.ENDPOINT_IN)
        if out is None or in_ is None:
            return None
        self.endpoints = (out.bEndpointAddress, in_.bEndpointAddress)
        return self.endpoints

    def read_session_us(self, device, ep_out, ep_in):
        """诊断页 0 的 session_us 回退即固件重启；仅 diag 构建可用。"""
        device.write(ep_out, pad([CMD_DIAG_PAGE, 0x01, 0x00]),
                     timeout=self.timeout_ms)
        page = bytes(device.read(ep_in, DAP_PACKET_SIZE, timeout=self.timeout_ms))
        if len(page) != DAP_PACKET_SIZE or page[:3] != bytes(
                (CMD_DIAG_PAGE, 0x01, 0x00)):
            raise usb.core.USBError("diag page 0 rejected: %s" % page[:4].hex())
        values = struct.unpack_from("<15I", page, 4)
        cycles_per_us = values[1]
        if not cycles_per_us:
            raise usb.core.USBError("diag cycles_per_us=0")
        return values[2] / cycles_per_us

    def probe_ep0(self, device):
        """接口之外的 EP0 证据：读 LANGID 串描述符，不依赖任何驱动绑定。"""
        return bytes(device.ctrl_transfer(0x80, 0x06, 0x0300, 0x0000, 255,
                                          timeout=self.timeout_ms))

    def probe_attach(self, device):
        """主机能否拿到设备句柄；返回 True/False/None（None = 探针不可用）。

        Windows 上这一层与“固件不应答”必须分开：没有任何接口绑定驱动时
        libusb_open 直接失败，而枚举列表与 PnP 节点仍然齐全——实测到的
        “设备在位 60s 却什么都做不了”就是卡在这一层。

        探针自拿不到底层对象时一律返回 None：不能把工具自己的 bug 记成
        “设备打不开”（连踩两次：传 Device 包装、传不存在的 .dev，都会造出
        假的 attach 失败）。
        """
        target = getattr(getattr(device, "_ctx", None), "dev", None)
        if target is None:
            return None, "attach-probe-unavailable"
        handle = None
        try:
            handle = self.backend.open_device(target)
            return True, None
        except Exception as error:  # libusb 对部分错误码抛 NotImplementedError
            return False, describe_error(error)
        finally:
            if handle is not None:
                try:
                    self.backend.close_device(handle)
                except Exception:
                    pass

    def inject_event(self):
        """在采样窗口内主动制造一次“刚连上”，把不可控的拔插变成可重现实验。

        reset 是 USB 总线复位：会重跑 SET_CONFIGURATION 与全部类 init（包括
        被怀疑的同号端点 setup 顺序），但不会让 Windows 重跑驱动安装；
        因此“总线复位后不重现”能把类 init 排除，“拔插重现但总线复位不重现”
        则指向主机侧绑定。
        """
        record = {"sensor": "inject", "t": time.monotonic() - self.started,
                  "wall": stamp(),
                  "kind": self.inject, "ok": False, "error": None}
        device = usb.core.find(idVendor=VID, idProduct=PID,
                               backend=self.backend)
        if device is None:
            record["error"] = "device absent, nothing to inject"
            self.injected = True
            print("[%7.2fs] 注入 %s：失败 %s" % (
                record["t"], record["kind"], record["error"]), flush=True)
            return record
        try:
            device.reset()
            record["ok"] = True
        except usb.core.USBError as error:
            record["error"] = describe_error(error)
        finally:
            # 总线复位后旧句柄与配置缓存均作废，下一个采样必须重读描述符。
            self.endpoints = None
            self.drain_pending = False
            try:
                device.finalize()
            except usb.core.USBError:
                pass
        self.injected = True
        self.injections.append(record["t"])
        print("[%7.2fs] 注入 %s：%s %s" % (
            record["t"], record["kind"], "ok" if record["ok"] else "失败",
            record["error"] or ""), flush=True)
        return record

    def sample(self):
        record = {"sensor": "dap", "t": time.monotonic() - self.started,
                  "wall": stamp(),
                  "device_present": False, "attach_ok": False,
                  "ep0_ok": False, "dap_ok": False,
                  "stage": "find", "error": None, "latency_us": None,
                  "drained": False, "session_us": None, "bcd_device": None,
                  "session_restarted": False, "diag_error": None}
        device = usb.core.find(idVendor=VID, idProduct=PID,
                               backend=self.backend)
        record["device_present"] = device is not None
        if device is None:
            return record
        # bcdDevice 由固件版本映射而来，且只读自描述符缓存：在 DAP 停摆窗口
        # 内仍能拿到，可用来证明“这一段里跑的仍是同一个镜像”。
        record["bcd_device"] = "0x%04X" % device.bcdDevice
        claimed = False
        try:
            record["stage"] = "attach"
            attached, error = self.probe_attach(device)
            if attached is False:
                record["error"] = error
                return record
            # None = 探针不可用：不能记成设备失败，后面的 EP0/bulk 会给出真信号。
            record["attach_ok"] = attached is True
            record["stage"] = "ep0"
            if len(self.probe_ep0(device)) >= 2:
                record["ep0_ok"] = True
            else:
                record["error"] = "ep0 returned short LANGID"
                return record
            record["stage"] = "descriptor"
            endpoints = self.resolve_endpoints(device)
            if endpoints is None:
                record["error"] = "interface %d has no bulk pair" % DAP_IF
                return record
            ep_out, ep_in = endpoints
            record["stage"] = "claim"
            usb.util.claim_interface(device, DAP_IF)
            claimed = True
            if self.drain_pending:
                # 上一次读超时后固件可能补交了迟到响应，先丢弃以免错配一问一答。
                try:
                    device.read(ep_in, DAP_PACKET_SIZE, timeout=20)
                    record["drained"] = True
                except usb.core.USBError:
                    pass
                self.drain_pending = False
            began = time.perf_counter()
            record["stage"] = "write"
            device.write(ep_out, pad([CMD_INFO, INFO_PROTOCOL_VERSION]),
                         timeout=self.timeout_ms)
            record["stage"] = "read"
            response = bytes(device.read(ep_in, DAP_PACKET_SIZE,
                                         timeout=self.timeout_ms))
            record["latency_us"] = (time.perf_counter() - began) * 1e6
            if len(response) < 2 or response[0] != CMD_INFO or \
                    response[1] == DAP_ERROR:
                record["stage"] = "response"
                record["error"] = "unexpected response %s" % response[:8].hex()
                return record
            record["dap_ok"] = True
            record["stage"] = None
            if self.should_read_diag(record["t"]):
                try:
                    record["session_us"] = self.read_session_us(
                        device, ep_out, ep_in)
                except usb.core.USBError as error:
                    record["diag_error"] = describe_error(error)
        except usb.core.USBError as error:
            record["error"] = describe_error(error)
            if record["stage"] == "read":
                self.drain_pending = True
        except (IndexError, KeyError, ValueError) as error:
            record["error"] = type(error).__name__ + ": " + str(error)
        except Exception as error:
            # 记录型工具死在一个采样上等于丢掉整个窗口；libusb 在某些
            # 错误码上不抛 USBError（如拔插瞬间的 NotImplementedError）。
            record["error"] = describe_error(error)
        finally:
            if claimed:
                try:
                    usb.util.release_interface(device, DAP_IF)
                except usb.core.USBError:
                    pass
            try:
                # pyusb 1.3 无 open/close；finalize 关闭底层句柄，使下一次采样
                # 真正重走一遍“能否附着该接口”，而不是复用旧句柄。
                device.finalize()
            except usb.core.USBError:
                pass
        return record

    def should_read_diag(self, now):
        if not self.diag_interval or self.last_diag is None:
            return False
        if now - self.last_diag < self.diag_interval:
            return False
        self.last_diag = now
        return True

    def watch_session(self, record):
        if record.get("session_us") is None:
            return
        if (self.last_session_us is not None and
                record["session_us"] < self.last_session_us):
            record["session_restarted"] = True
        self.last_session_us = record["session_us"]

    def report(self, record):
        if self.last_dap_ok is None or record["dap_ok"] != self.last_dap_ok:
            state = "DAP 可用" if record["dap_ok"] else "DAP 不可用"
            print("[%7.2fs] %s device=%s attach=%s ep0=%s stage=%s %s" % (
                record["t"], state, record["device_present"],
                record["attach_ok"], record["ep0_ok"],
                record["stage"], record["error"] or ""), flush=True)
            self.last_dap_ok = record["dap_ok"]

    def run(self):
        log = self.open_log()
        host = HostSampler(self.started, 1.0)
        if self.with_host:
            host.start()
        deadline = self.started + self.seconds
        logged_host = 0
        snapshots = []
        print("监控中：现在可以插入/复位/重新烧录板子；%.0fs 后自动结束"
              % self.seconds, flush=True)

        def write(record):
            if log is not None:
                log.write(json.dumps(record, ensure_ascii=False) + "\n")
                log.flush()

        try:
            while time.monotonic() < deadline:
                began = time.monotonic()
                record = self.sample()
                self.watch_session(record)
                self.samples.append(record)
                self.report(record)
                write(record)
                if (self.inject and not self.injected and
                        (began - self.started) >= self.inject_at):
                    write(self.inject_event())
                # 主机侧快照与 DAP 采样一起写进同一 JSONL，保持时间轴对齐。
                snapshots = host.take()
                while logged_host < len(snapshots):
                    write(snapshots[logged_host])
                    logged_host += 1
                pause = self.interval - (time.monotonic() - began)
                if pause > 0:
                    time.sleep(pause)
        except KeyboardInterrupt:
            print("收到 Ctrl-C，提前结束并汇总已采集数据", flush=True)
        finally:
            host.stop_event.set()
            snapshots = host.take()
            while logged_host < len(snapshots):
                write(snapshots[logged_host])
                logged_host += 1
            if log is not None:
                log.close()
        return snapshots


def pad(payload):
    return bytearray(list(payload) + [0] * (DAP_PACKET_SIZE - len(payload)))


def load_capture(path):
    """读回一份已采集的 JSONL，用于不碰硬件地重新分诊。"""
    samples = []
    snapshots = []
    injections = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            record = json.loads(line)
            sensor = record.get("sensor")
            if sensor == "dap":
                samples.append(record)
            elif sensor == "host":
                snapshots.append(record)
            elif sensor == "inject":
                injections.append(record["t"])
    return samples, snapshots, injections


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seconds", type=float, default=180.0)
    parser.add_argument("--interval", type=float, default=0.15,
                        help="采样间隔秒数；要看清毫秒级重枚举请用 0.05")
    parser.add_argument("--timeout", type=float, default=0.3,
                        help="单次 bulk 传输超时秒数")
    parser.add_argument("--replay",
                        help="不采新数据，只对已有 JSONL 重新分诊")
    parser.add_argument("--diag", type=float, default=0.0,
                        help="每 N 秒读一次诊断页 0 的 session_us 以捕捉固件重启"
                             "（需 diag 构建，0 表示关闭）")
    parser.add_argument("--out", default="build/usb_dap_watch.jsonl")
    parser.add_argument("--no-host", action="store_true",
                        help="不采集 Windows PnP/磁盘节点快照")
    parser.add_argument("--inject", choices=("reset",),
                        help="在窗口内主动制造一次重新枚举：reset=USB 总线复位"
                             "（重跑类 init，但不重跑 Windows 驱动安装）")
    parser.add_argument("--inject-at", type=float, default=2.0,
                        help="注入时刻（从监控开始算起秒数）")
    arguments = parser.parse_args()

    if arguments.replay:
        samples, snapshots, injections = load_capture(arguments.replay)
        arrival = arrival_time(samples, snapshots)
        events = classify_stalls(samples, snapshots, injections, arrival)
        print("replay %s：插入时刻 %.2fs，故障窗口 %d 个"
              % (arguments.replay, arrival if arrival is not None else -1.0,
                 len(events)))
        print(render_summary(samples, events, snapshots, arrival))
        return 0

    watcher = DapWatcher(arguments.interval, arguments.seconds,
                         int(arguments.timeout * 1000), arguments.diag,
                         arguments.out, not arguments.no_host,
                         arguments.inject, arguments.inject_at)
    if arguments.diag:
        watcher.last_diag = 0.0
    snapshots = watcher.run()
    arrival = arrival_time(watcher.samples, snapshots)
    events = classify_stalls(watcher.samples, snapshots, watcher.injections,
                             arrival)
    print()
    print(render_summary(watcher.samples, events, snapshots, arrival))
    if arguments.out:
        print("\n原始采样: %s" % arguments.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
