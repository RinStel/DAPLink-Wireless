import unittest

from tools import usb_dap_watch


def sample(t, dap_ok, device_present=True, attach_ok=True, ep0_ok=True,
           stage=None, error=None, bcd_device="0x015B"):
    return {"t": t, "dap_ok": dap_ok, "device_present": device_present,
            "attach_ok": attach_ok, "ep0_ok": ep0_ok, "stage": stage,
            "error": error, "bcd_device": bcd_device if device_present else None,
            "latency_us": 300.0 if dap_ok else None}


def host(t, pnp_state="ok", instances=("USB\\VID_28E9&PID_1290\\656C6C680015",),
         msc_disks=("OK|DAPLINK CONFIG DISK USB Device",), com_ports=("COM5",),
         cdc_open=(), error=None):
    return {"t": t, "pnp_state": pnp_state, "pnp_problems": [],
            "pnp_instances": list(instances), "msc_disks": list(msc_disks),
            "com_ports": list(com_ports), "cdc_open": list(cdc_open),
            "error": error}


ABSENT = host(0.5, pnp_state="absent", instances=(), msc_disks=(),
              com_ports=())


class HostSnapshotParseTest(unittest.TestCase):
    def test_problem_code_zero_is_healthy_and_text_is_not(self):
        # Get-PnpDevice 的 Problem 枚举已转成 int：判据必须是编号而不是文本。
        healthy = ("pnp|USB\\VID_28E9&PID_1290&MI_00\\8&b&0000|OK|0")
        broken = ("pnp|USB\\VID_28E9&PID_1290&MI_00\\8&b&0000|Unknown|1")

        self.assertEqual(
            usb_dap_watch.parse_host_snapshot(healthy, [])["pnp_state"], "ok")
        self.assertEqual(
            usb_dap_watch.parse_host_snapshot(broken, [])["pnp_state"],
            "problem")

    def test_snapshot_collects_nodes_disks_and_ports(self):
        text = "\n".join([
            "pnp|USB\\VID_28E9&PID_1290\\656C6C680015|OK|",
            "pnp|USB\\VID_28E9&PID_1290&MI_03\\8&B818196&0&0003|OK|",
            "disk|OK|DAPLINK CONFIG DISK USB Device",
        ])

        parsed = usb_dap_watch.parse_host_snapshot(text, ["COM5"])

        self.assertEqual(parsed["pnp_state"], "ok")
        self.assertEqual(len(parsed["pnp_instances"]), 2)
        self.assertEqual(parsed["msc_disks"],
                         ["OK|DAPLINK CONFIG DISK USB Device"])
        self.assertEqual(parsed["com_ports"], ["COM5"])

    def test_disk_witness_excludes_volatile_drive_identity(self):
        parsed = usb_dap_watch.parse_host_snapshot(
            "disk|OK|DAPLINK CONFIG DISK USB Device", [])

        # 判据里不能含盘符/PHYSICALDRIVE 序号，否则重枚举后的编号漂移会造出假跳变。
        self.assertNotIn("PHYSICALDRIVE", parsed["msc_disks"][0])
        self.assertNotIn("D:", parsed["msc_disks"][0])

    def test_non_ok_node_is_reported_as_problem(self):
        text = "pnp|USB\\VID_28E9&PID_1290&MI_03\\8&B818196&0&0003|Error|10"

        parsed = usb_dap_watch.parse_host_snapshot(text, [])

        self.assertEqual(parsed["pnp_state"], "problem")
        self.assertEqual(parsed["pnp_problems"],
                         ["USB\\VID_28E9&PID_1290&MI_03\\8&B818196&0&0003"
                          ":Error/10"])
        self.assertEqual(parsed["msc_disks"], [])

    def test_empty_snapshot_means_absent(self):
        parsed = usb_dap_watch.parse_host_snapshot("", [])

        self.assertEqual(parsed["pnp_state"], "absent")


class ClassifyStallsTest(unittest.TestCase):
    def test_failures_before_first_success_are_not_reported(self):
        samples = [sample(0.0, False, device_present=False, ep0_ok=False,
                          stage="find"),
                   sample(0.2, False, device_present=False, ep0_ok=False,
                          stage="find"),
                   sample(5.0, True)]

        events = usb_dap_watch.classify_stalls(samples, [ABSENT])

        self.assertEqual(events, [])

    def test_window_after_arrival_is_not_swallowed_by_first_success(self):
        # 旧实现拿“首次成功”当分界，会把插入后整段 60s 吐成 pre-arrival。
        samples = [sample(0.0, False, device_present=False, attach_ok=False,
                          ep0_ok=False, stage="find"),
                   sample(2.0, False, attach_ok=False, ep0_ok=False,
                          stage="attach", error="errno=2 Entity not found"),
                   sample(60.0, True)]
        snapshots = [ABSENT, host(1.5, msc_disks=())]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertEqual(len(events), 1)
        self.assertAlmostEqual(events[0]["start_s"], 2.0)
        self.assertFalse(events[0]["device_gone"])
        self.assertTrue(events[0]["attach_failed"])

    def test_interface_only_stall_keeps_all_bystanders_healthy(self):
        samples = [sample(0.0, True)]
        samples += [sample(0.2 + index * 0.2, False, stage="read",
                           error="errno=110 Timeout") for index in range(6)]
        samples.append(sample(2.0, True))
        snapshots = [host(t) for t in (0.0, 1.0, 2.0)]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["samples"], 6)
        self.assertEqual(events[0]["stages"], {"read": 6})
        self.assertFalse(events[0]["device_gone"])
        self.assertFalse(events[0]["ep0_failed"])
        self.assertTrue(events[0]["diagnosis"].startswith("interface-only-stall"))

    def test_disk_churn_during_stall_is_a_device_dropout(self):
        samples = [sample(0.0, True)]
        samples += [sample(0.2, False, stage="write"),
                    sample(0.4, False, stage="find", device_present=False,
                           ep0_ok=False),
                    sample(0.6, True)]
        snapshots = [host(0.0), host(0.5, msc_disks=())]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertEqual(len(events), 1)
        self.assertTrue(events[0]["device_gone"])
        self.assertTrue(events[0]["host"]["disk_lost"])
        self.assertTrue(events[0]["diagnosis"].startswith("device-dropout"))

    def test_attaching_is_separated_from_ep0_deafness(self):
        # 实板形状：设备一直在总线上，只是主机打不开句柄，磁盘也还没建对象。
        samples = [sample(1.0, False, attach_ok=False, ep0_ok=False,
                          stage="attach", error="errno=2 Entity not found"),
                   sample(1.2, False, attach_ok=False, ep0_ok=False,
                          stage="attach", error="errno=2 Entity not found"),
                   sample(61.6, True)]
        snapshots = [ABSENT,
                     host(1.5, msc_disks=()),
                     host(61.5, msc_disks=("OK|DAPLINK CONFIG DISK USB Device",))]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertEqual(len(events), 1)
        self.assertTrue(events[0]["attach_failed"])
        # “磁盘后来才出现”是晚启动，不是重枚举。
        self.assertFalse(events[0]["host"]["disk_lost"])
        self.assertTrue(events[0]["host"]["disk_late"])
        self.assertTrue(events[0]["diagnosis"].startswith("host-attach-gap"))

    def test_ep0_silence_is_separated_from_bulk_stall(self):
        samples = [sample(0.0, True)]
        samples += [sample(0.2, False, ep0_ok=False, stage="ep0",
                           error="errno=110 Timeout"),
                    sample(0.4, True)]
        snapshots = [host(0.0), host(0.3)]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertTrue(events[0]["ep0_failed"])
        self.assertFalse(events[0]["attach_failed"])
        self.assertTrue(events[0]["diagnosis"].startswith("ep0-stall"))

    def test_missing_pnp_node_without_dropout_is_a_binding_gap(self):
        samples = [sample(0.0, True)]
        samples += [sample(0.2, False, stage="claim", error="errno=2 Not found"),
                    sample(0.4, True)]
        snapshots = [host(0.0), host(0.3, pnp_state="problem"), host(0.6)]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertEqual(len(events), 1)
        self.assertFalse(events[0]["device_gone"])
        self.assertTrue(events[0]["diagnosis"].startswith("host-binding-gap"))

    def test_cdc_open_during_window_proves_the_firmware_is_alive(self):
        # 打开 CDC 会迫使 usbser 经 EP0 发 SET_LINE_CODING：它能开 = 固件在应答。
        samples = [sample(2.0, False, attach_ok=False, ep0_ok=False,
                          stage="attach", error="errno=2 Entity not found"),
                   sample(2.2, False, attach_ok=False, ep0_ok=False,
                          stage="attach", error="errno=2 Entity not found")]
        snapshots = [host(2.1, msc_disks=(), cdc_open=("COM5:OK",))]

        events = usb_dap_watch.classify_stalls(samples, snapshots)

        self.assertTrue(events[0]["host"]["cdc_alive"])
        self.assertTrue(events[0]["diagnosis"].startswith("function-gap"))

    def test_cdc_busy_is_not_counted_as_alive(self):
        self.assertFalse(usb_dap_watch.cdc_alive(["COM5:BUSY"]))
        self.assertFalse(usb_dap_watch.cdc_alive([]))
        self.assertTrue(usb_dap_watch.cdc_alive(["COM5:OK"]))

    def test_unavailable_attach_probe_is_not_a_device_failure(self):
        class Stub:
            backend = None
            probe_attach = usb_dap_watch.DapWatcher.probe_attach

        # 拿不到底层对象时必须报“未知”，不能把工具自己的 bug 当成设备故障。
        alive, error = Stub().probe_attach(object())

        self.assertIsNone(alive)
        self.assertEqual(error, "attach-probe-unavailable")

    def test_stall_open_at_shutdown_is_flagged_truncated(self):
        samples = [sample(0.0, True)]
        samples += [sample(0.2 + index * 0.2, False, stage="read")
                    for index in range(4)]

        events = usb_dap_watch.classify_stalls(samples, [host(0.0)])

        self.assertEqual(len(events), 1)
        self.assertTrue(events[0]["truncated"])
        self.assertIn("duration_s", events[0])

    def test_two_separate_windows_are_two_events(self):
        samples = [sample(0.0, True),
                   sample(0.2, False, stage="read"),
                   sample(0.4, True),
                   sample(0.6, False, stage="read"),
                   sample(0.8, False, stage="read"),
                   sample(1.0, True)]

        events = usb_dap_watch.classify_stalls(samples, [host(0.5)])

        self.assertEqual([event["samples"] for event in events], [1, 2])

    def test_summary_reports_latency_and_window_count(self):
        samples = [sample(0.0, True), sample(0.2, False, stage="read"),
                   sample(0.4, True)]
        snapshots = [host(0.0), host(0.4)]

        events = usb_dap_watch.classify_stalls(samples, snapshots)
        summary = usb_dap_watch.render_summary(samples, events, snapshots)

        self.assertIn("DAP 可用 2 次", summary)
        self.assertIn("窗口 1", summary)
        self.assertIn("interface-only-stall", summary)

    def test_worst_pnp_state_in_window_is_not_masked_by_recovery(self):
        samples = [sample(0.0, True), sample(0.2, False, stage="claim"),
                   sample(0.4, True)]
        snapshots = [host(0.0, pnp_state="problem"), host(0.6)]

        events = usb_dap_watch.classify_stalls(samples, snapshots)
        summary = usb_dap_watch.render_summary(samples, events, snapshots)

        # 绑定间隙是瞬态的，窗口内最差的 PnP 状态必须赢过末次状态。
        self.assertEqual(events[0]["host"]["pnp_state"], "problem")
        self.assertIn("host-binding-gap", summary)
        self.assertFalse("interface-only-stall" in summary)

    def test_summary_reports_host_baseline(self):
        samples = [sample(0.0, True)]
        snapshots = [host(0.0, com_ports=("COM5", "COM9"))]

        summary = usb_dap_watch.render_summary(
            samples, usb_dap_watch.classify_stalls(samples, snapshots),
            snapshots)

        self.assertIn("COM5,COM9", summary)
        self.assertIn("MSC 磁盘 1", summary)

    def test_bcd_device_is_a_firmware_witness_available_during_stalls(self):
        # bcdDevice 只读自描述符，DAP 停摆时仍能拿到，所以窗口内也能区分镜像。
        samples = [sample(0.0, True),
                   sample(0.2, False, stage="read", bcd_device="0x015B"),
                   sample(0.4, True, bcd_device="0x015C")]

        summary = usb_dap_watch.render_summary(
            samples, usb_dap_watch.classify_stalls(samples, []), [])

        self.assertIn("0x015B, 0x015C", summary)
        self.assertIn("镜像变了", summary)


if __name__ == "__main__":
    unittest.main()
