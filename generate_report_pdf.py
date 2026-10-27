import sys
from fpdf import FPDF

class PDFReport(FPDF):
    def header(self):
        self.set_font('Helvetica', 'B', 14)
        self.set_text_color(26, 31, 44)
        self.cell(0, 7, "ENGINEERING AUDIT & DEFECT REPORT", ln=True)
        self.set_font('Helvetica', '', 9)
        self.set_text_color(100, 100, 100)
        self.cell(0, 5, "USB-CDC Communication Failure Analysis | ICD SPEC-REV 2.4", ln=True)
        self.set_draw_color(3, 102, 214)
        self.set_line_width(0.8)
        self.line(10, self.get_y() + 2, 200, self.get_y() + 2)
        self.ln(5)

    def footer(self):
        self.set_y(-12)
        self.set_font('Helvetica', 'I', 8)
        self.set_text_color(130, 130, 130)
        self.cell(0, 8, f"ICD SPEC-REV 2.4 Bridge Audit Report  |  Page {self.page_no()} of {{nb}}  |  Confidential", align='C')

def create_report(output_paths):
    pdf = PDFReport(orientation='P', unit='mm', format='A4')
    pdf.alias_nb_pages()
    pdf.set_auto_page_break(auto=True, margin=15)
    pdf.add_page()

    # Meta Table Box
    pdf.set_fill_color(246, 248, 250)
    pdf.set_draw_color(220, 224, 230)
    pdf.rect(10, pdf.get_y(), 190, 24, style='FD')
    
    pdf.set_y(pdf.get_y() + 2)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    
    # Row 1
    pdf.set_x(12)
    pdf.cell(32, 5, "Target Repository:", 0, 0)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(3, 102, 214)
    pdf.cell(60, 5, "crazyflie-android-client", 0, 0)
    
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    pdf.cell(30, 5, "Hardware Target:", 0, 0)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(3, 102, 214)
    pdf.cell(60, 5, "Seeed Studio XIAO ESP32-C6", 0, 1)

    # Row 2
    pdf.set_x(12)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    pdf.cell(32, 5, "Protocol Contract:", 0, 0)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(3, 102, 214)
    pdf.cell(60, 5, "ICD SPEC-REV 2.4 (0x31 / 0x51)", 0, 0)

    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    pdf.cell(30, 5, "Stream Encoding:", 0, 0)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(3, 102, 214)
    pdf.cell(60, 5, "COBS + CRC-16/CCITT-FALSE", 0, 1)

    # Row 3
    pdf.set_x(12)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    pdf.cell(32, 5, "Observed Status:", 0, 0)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(60, 5, "0 Bytes Received (Failsafe Active)", 0, 0)

    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(60, 60, 60)
    pdf.cell(30, 5, "Severity Rating:", 0, 0)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(60, 5, "CRITICAL BLOCKER", 0, 1)
    
    pdf.ln(6)

    # Section 1: Executive Summary
    pdf.set_font('Helvetica', 'B', 11)
    pdf.set_text_color(26, 31, 44)
    pdf.cell(0, 6, "1. Executive Summary", ln=True)
    pdf.set_draw_color(220, 220, 220)
    pdf.line(10, pdf.get_y(), 200, pdf.get_y())
    pdf.ln(2)

    pdf.set_font('Helvetica', '', 9)
    pdf.set_text_color(40, 40, 40)
    summary_text = (
        "During bench integration between the Seeed Studio XIAO ESP32-C6 and the Crazyflie Android "
        "Client via direct USB-C OTG cable, the ESP32 receiver immediately drops into Airborne Failsafe. "
        "Diagnostics on the ESP32 confirm that precisely 0 bytes are received over USB.\n\n"
        "An in-depth architectural audit of the Android repository confirmed that while the core packet "
        "definitions (EspIcdCommanderPacket, Cobs, Crc16Ccitt) adhere strictly to ICD SPEC-REV 2.4, "
        "four critical flaws in device discovery, USB interface claiming, and packet dispatch completely "
        "prevent the Android app from opening the USB port or transmitting any data."
    )
    pdf.multi_cell(0, 4.5, summary_text)
    pdf.ln(3)

    # Section 2: Defect Overview Table
    pdf.set_font('Helvetica', 'B', 11)
    pdf.set_text_color(26, 31, 44)
    pdf.cell(0, 6, "2. Summary of Identified Defects", ln=True)
    pdf.line(10, pdf.get_y(), 200, pdf.get_y())
    pdf.ln(2)

    # Table Header
    pdf.set_fill_color(240, 243, 246)
    pdf.set_font('Helvetica', 'B', 8.5)
    pdf.set_text_color(30, 30, 30)
    pdf.cell(8, 6, "#", 1, 0, 'C', fill=True)
    pdf.cell(50, 6, "Source Location", 1, 0, 'L', fill=True)
    pdf.cell(65, 6, "Defect Description", 1, 0, 'L', fill=True)
    pdf.cell(67, 6, "Direct System Consequence", 1, 1, 'L', fill=True)

    defects_summary = [
        ("1", "MainActivity.java:746", "Hardcoded Crazyradio Nordic VID/PID filter (0x1915 / 0x7777).", "App sees ESP32 as unavailable, aborts USB open, falls back to BLE toast."),
        ("2", "UsbLinkAndroid.java:112", "Hardcoded getInterface(0) expecting >= 2 endpoints.", "ESP32 CDC Control Interface has 1 endpoint; fails with 'Could not find endpoints'."),
        ("3", "MainPresenter.java:224", "sendPacket() sent to legacy Crazyflie / RadioDriver pipeline.", "toCobsUsbFrame() and bulkWrite() are dead code; 0 packets sent over USB."),
        ("4", "MainActivity.java:753", "mTelemetryListener is never registered with UsbLinkAndroid.", "Uplink 0x51 telemetry frames decoded by USB reader never reach the UI.")
    ]

    pdf.set_font('Helvetica', '', 8)
    for num, loc, desc, cons in defects_summary:
        pdf.cell(8, 7, num, 1, 0, 'C')
        pdf.set_text_color(3, 102, 214)
        pdf.cell(50, 7, loc, 1, 0, 'L')
        pdf.set_text_color(40, 40, 40)
        pdf.cell(65, 7, desc, 1, 0, 'L')
        pdf.cell(67, 7, cons, 1, 1, 'L')

    pdf.ln(4)

    # Section 3: Deep Dive
    pdf.set_font('Helvetica', 'B', 11)
    pdf.set_text_color(26, 31, 44)
    pdf.cell(0, 6, "3. Technical Analysis & Code Remediation", ln=True)
    pdf.line(10, pdf.get_y(), 200, pdf.get_y())
    pdf.ln(3)

    # Defect 1 Box
    pdf.set_font('Helvetica', 'B', 9.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(0, 5, "Defect 1: Hardcoded Crazyradio VID / PID Filter in USB Discovery", ln=True)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(40, 40, 40)
    pdf.multi_cell(0, 4.2, 
        "Location: MainActivity.java (lines 746-751)\n"
        "Problem: isCrazyradioAvailable() specifically queries UsbLinkAndroid.findUsbDevices() using "
        "Crazyradio.CRADIO_VID (0x1915) and CRADIO_PID (0x7777). Because the Seeed Studio XIAO ESP32-C6 "
        "reports VID 0x2886 / 0x303A and PID 0x0048 / 0x1001, discovery returns an empty list.\n"
        "Consequence: When 'Connect' is tapped, isCrazyradioAvailable() evaluates false. The app calls "
        "connectBlePreChecks(), displays a toast 'USB OTG is the sole supported transport link.', and halts."
    )
    pdf.ln(1)
    
    # Code block 1
    pdf.set_font('Courier', '', 7.5)
    pdf.set_fill_color(245, 247, 250)
    code_fix_1 = (
        "// In MainActivity.java:\n"
        "- List<UsbDevice> usbDeviceList = UsbLinkAndroid.findUsbDevices(usbManager, (short) Crazyradio.CRADIO_VID, (short) Crazyradio.CRADIO_PID);\n"
        "+ // Accept Seeed Studio (0x2886) and Espressif (0x303A) or any connected USB CDC device:\n"
        "+ List<UsbDevice> usbDeviceList = UsbLinkAndroid.findEspOrCrazyDevices(usbManager);"
    )
    pdf.multi_cell(0, 3.8, code_fix_1, border=1, fill=True)
    pdf.ln(3)

    # Defect 2 Box
    pdf.set_font('Helvetica', 'B', 9.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(0, 5, "Defect 2: USB CDC-ACM Multi-Interface & Endpoint Enumeration Bug", ln=True)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(40, 40, 40)
    pdf.multi_cell(0, 4.2, 
        "Location: UsbLinkAndroid.java (lines 112-120)\n"
        "Problem: Standard USB CDC-ACM defines two interfaces: Interface 0 (CDC Control, 1 endpoint) "
        "and Interface 1 (CDC Data, 2 bulk endpoints: Bulk IN & Bulk OUT). Line 112 queries getInterface(0) "
        "and checks if (mIntf.getEndpointCount() < 2). On the ESP32-C6, Interface 0 has only 1 endpoint.\n"
        "Consequence: The condition mIntf.getEndpointCount() < 2 is always true. It logs 'Could not find endpoints' "
        "and returns without claiming the interface or establishing the connection."
    )
    pdf.ln(1)

    # Code block 2
    pdf.set_font('Courier', '', 7.5)
    code_fix_2 = (
        "// In UsbLinkAndroid.java:\n"
        "- mIntf = this.mUsbDevice.getInterface(0);\n"
        "- if (mIntf.getEndpointCount() < 2) { Log.e(LOG_TAG, \"Could not find endpoints\"); return; }\n"
        "+ // Iterate through all interfaces to locate the CDC Data interface containing Bulk endpoints:\n"
        "+ for (int i = 0; i < mUsbDevice.getInterfaceCount(); i++) {\n"
        "+     UsbInterface iface = mUsbDevice.getInterface(i);\n"
        "+     for (int j = 0; j < iface.getEndpointCount(); j++) {\n"
        "+         UsbEndpoint ep = iface.getEndpoint(j);\n"
        "+         if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {\n"
        "+             if (ep.getDirection() == UsbConstants.USB_DIR_IN) mEpIn = ep;\n"
        "+             if (ep.getDirection() == UsbConstants.USB_DIR_OUT) mEpOut = ep;\n"
        "+         }\n"
        "+     }\n"
        "+     if (mEpIn != null && mEpOut != null) { mIntf = iface; break; }\n"
        "+ }"
    )
    pdf.multi_cell(0, 3.8, code_fix_2, border=1, fill=True)
    pdf.ln(3)

    # Page break for Defects 3 & 4
    pdf.add_page()

    # Defect 3 Box
    pdf.set_font('Helvetica', 'B', 9.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(0, 5, "Defect 3: Packet Pipeline Bypass (toCobsUsbFrame & bulkWrite Never Executed)", ln=True)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(40, 40, 40)
    pdf.multi_cell(0, 4.2, 
        "Location: MainPresenter.java (lines 224-230) vs EspIcdCommanderPacket.java (line 99)\n"
        "Problem: EspIcdCommanderPacket.toCobsUsbFrame() constructs the 11B CRTP + 2B CRC16 + COBS frame with 0x00 delimiter. "
        "However, MainPresenter calls sendPacket(icdPacket), which routes into the legacy Crazyflie -> RadioDriver pipeline. "
        "Because there is no 2.4GHz Nordic Crazyradio dongle attached, mDriver.isConnected() evaluates false, and all packets "
        "are discarded silently. Neither toCobsUsbFrame() nor UsbLinkAndroid.bulkWrite() is ever called anywhere in the app.\n"
        "Consequence: Exactly 0 bytes are transmitted over the physical USB bus."
    )
    pdf.ln(1)

    # Code block 3
    pdf.set_font('Courier', '', 7.5)
    code_fix_3 = (
        "// In MainPresenter.java (startSendJoystickDataThread):\n"
        "  EspIcdCommanderPacket icdPacket = new EspIcdCommanderPacket(rollNorm, pitchNorm, yawNorm, thrustNorm, ...);\n"
        "- sendPacket(icdPacket);\n"
        "+ // Transmit COBS-encoded CRTP frame directly over USB-CDC as per ICD SPEC-REV 2.4:\n"
        "+ if (mUsbLink != null && mUsbLink.isUsbConnected()) {\n"
        "+     byte[] frame = icdPacket.toCobsUsbFrame();\n"
        "+     mUsbLink.bulkWrite(frame);\n"
        "+ }"
    )
    pdf.multi_cell(0, 3.8, code_fix_3, border=1, fill=True)
    pdf.ln(3)

    # Defect 4 Box
    pdf.set_font('Helvetica', 'B', 9.5)
    pdf.set_text_color(215, 58, 73)
    pdf.cell(0, 5, "Defect 4: Telemetry Listener Disconnected from UI Dashboard", ln=True)
    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(40, 40, 40)
    pdf.multi_cell(0, 4.2, 
        "Location: UsbLinkAndroid.java (lines 132-155) vs MainActivity.java (line 753)\n"
        "Problem: UsbLinkAndroid starts a background thread that successfully reads, COBS-decodes, and CRC-verifies "
        "incoming 6-byte 0x51 telemetry frames from the drone. MainActivity also provides updateTelemetry(voltageMv, currentMa, pps). "
        "However, mUsbLink.setTelemetryListener(...) is never invoked during initialization.\n"
        "Consequence: Even when downlink frames are restored, incoming telemetry from the ESP32 is orphaned, leaving battery "
        "and PPS indicators at zero."
    )
    pdf.ln(1)

    # Code block 4
    pdf.set_font('Courier', '', 7.5)
    code_fix_4 = (
        "// In MainActivity.java or MainPresenter.java upon USB initialization:\n"
        "+ mUsbLink.setTelemetryListener(new UsbLinkAndroid.TelemetryListener() {\n"
        "+     @Override\n"
        "+     public void onTelemetryReceived(int voltageMv, int currentMa, int pps) {\n"
        "+         mainActivity.updateTelemetry(voltageMv, currentMa, pps);\n"
        "+     }\n"
        "+     @Override\n"
        "+     public void onWatchdogTimeout() {\n"
        "+         mainActivity.setLinkQualityText(\"LOST\");\n"
        "+     }\n"
        "+ });"
    )
    pdf.multi_cell(0, 3.8, code_fix_4, border=1, fill=True)
    pdf.ln(4)

    # Section 4: Testing & Verification
    pdf.set_font('Helvetica', 'B', 11)
    pdf.set_text_color(26, 31, 44)
    pdf.cell(0, 6, "4. Verification & Validation Protocol", ln=True)
    pdf.line(10, pdf.get_y(), 200, pdf.get_y())
    pdf.ln(2)

    pdf.set_font('Helvetica', '', 8.5)
    pdf.set_text_color(40, 40, 40)
    verif_text = (
        "1. Rebuild Android app with the above four patches and deploy to mobile device.\n"
        "2. Connect Seeed Studio XIAO ESP32-C6 via USB-C OTG cable; tap 'OK' on Android USB permission dialog.\n"
        "3. Observe Built-In LED on GPIO 15 (Active-LOW PWM):\n"
        "   - Idle / Cutoff (Throttle = 1000 us)  -> LED is completely OFF.\n"
        "   - Mid-Stick     (Throttle = 1500 us)  -> LED glows at ~25% intensity.\n"
        "   - Full Throttle (Throttle = 2000 us)  -> LED glows at 100% full brightness.\n"
        "4. Connect phone browser to Wi-Fi 'XIAO-C6-DEBUG' at http://192.168.4.1 to verify 50 PPS rate & 0 errors."
    )
    pdf.multi_cell(0, 4.3, verif_text)

    # Output to all desired paths
    for path in output_paths:
        pdf.output(path)
        print(f"Report written to: {path}")

if __name__ == '__main__':
    paths = [
        r"C:\Users\dipan\OneDrive\Desktop\Android_App_USB_Communication_Issues_Report.pdf",
        r"c:\Users\dipan\OneDrive\Dokumen\PlatformIO\Projects\DSI link test\Android_App_USB_Communication_Issues_Report.pdf"
    ]
    create_report(paths)
