import 'package:flutter/material.dart';
import 'dart:math';
import 'package:fl_chart/fl_chart.dart';
import 'package:flutter_libserialport/flutter_libserialport.dart';
import 'pdf_export.dart';
import 'dart:typed_data';

void main() {
  runApp(const BMSApp());
}


// ─────────────────────────────────────────
// DATA MODEL
// ─────────────────────────────────────────
class CellData {
  double voltage;
  double current;
  double soc;
  double temperature;
  double capacity;

  CellData({
    required this.voltage,
    required this.current,
    required this.soc,
    required this.temperature,
    required this.capacity,
  });

  CellData copyWith({
    double? voltage,
    double? current,
    double? soc,
    double? temperature,
    double? capacity,
  }) =>
      CellData(
        voltage: voltage ?? this.voltage,
        current: current ?? this.current,
        soc: soc ?? this.soc,
        temperature: temperature ?? this.temperature,
        capacity: capacity ?? this.capacity,
      );
}

// ─────────────────────────────────────────
// APP
// ─────────────────────────────────────────
class BMSApp extends StatelessWidget {
  const BMSApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'BMS Battery Monitor',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSwatch(primarySwatch: Colors.deepPurple)
            .copyWith(secondary: Colors.deepPurpleAccent),
        fontFamily: 'Segoe UI',
        scaffoldBackgroundColor: const Color(0xFFF0F2F7),
      ),
      home: const BMSHomeScreen(),
    );
  }
}

// ─────────────────────────────────────────
// HOME SCREEN
// ─────────────────────────────────────────
class BMSHomeScreen extends StatefulWidget {
  const BMSHomeScreen({super.key});

  @override
  State<BMSHomeScreen> createState() => _BMSHomeScreenState();
}

//TickerProviderStateMixin là một class giúp tạo chuyển động mượt
class _BMSHomeScreenState extends State<BMSHomeScreen>
    with TickerProviderStateMixin {
  int _selectedIndex = 0;
  String _incomingBuffer = ''; //nơi chứa dữ liệu gửi đến
  late AnimationController _waveController;

  // ── Serial State ────────────────────────
  SerialPort? _port;
  SerialPortReader? _reader;
  List<String> _availablePorts = [];
  String? _selectedPort;
  bool _isConnected = false;
  bool _isRunning = false;

  // ── State ──────────────────────────────
  List<CellData> cells = [
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
    CellData(voltage: 0, current: 0, soc: 0, temperature: 0, capacity: 0),
  ];

  // ── Dòng điện tổng của pack (giá trị "current" cuối gói tin, đơn vị A, có dấu) ──
  double packCurrent = 0.0;
  String chargeFlag = '0';

  // ── Lịch sử SOC-Thời gian cho biểu đồ (6 cell) ──
  final List<List<FlSpot>> socTimeHistory =
  List.generate(6, (_) => <FlSpot>[]);
  static const int _maxHistoryPoints = 5000;

  // ── Mốc thời gian bắt đầu (x = 0) của phiên đang được vẽ trên đồ thị ──
  DateTime? _chartStartTime;

  // ── Đánh dấu cell nào đã đạt SOC 100% để dừng ghi thêm điểm vào đồ thị ──
  final List<bool> _chartFinished = List.generate(6, (_) => false);

  // ── Lịch sử dữ liệu của phiên ghi hiện tại (từ lúc nhấn START đến lúc nhấn STOP) ──
  // Ghi mỗi 1 phút/lần, CHỈ ghi trong lúc _isRunning == true
  final List<List<Map<String, dynamic>>> sessionHistory =
  List.generate(6, (_) => <Map<String, dynamic>>[]);
  DateTime? _lastLogTime;
  static const Duration _logInterval = Duration(minutes: 1);

  // ── Đánh dấu dữ liệu phiên hiện tại đã được xuất PDF hay chưa ──
  // true = đã xuất (hoặc chưa có dữ liệu gì), false = có dữ liệu nhưng CHƯA xuất
  bool _sessionExported = true;

  @override
  //Hàm dùng để tạo ra sự chuyển động của sóng với chu kỳ 2s
  void initState() {
    super.initState();
    _waveController = AnimationController(
      vsync: this,  //chỉ vẽ khi khung hình da san sang
      duration: const Duration(seconds: 2),
    )..repeat();

    // Quét cổng COM ngay khi mở app
    _refreshPorts();
  }
  void _showCustomSnackBar(String message, {bool isError = false}) {
    // Xóa SnackBar cũ ngay lập tức để hiện cái mới (tạo cảm giác nhạy)
    ScaffoldMessenger.of(context).clearSnackBars();

    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Row(
          children: [
            Icon(
                isError ? Icons.error_outline : Icons.info_outline,
                color: isError ? Colors.red.shade600 : Colors.blue.shade600,
                size: 20
            ),
            const SizedBox(width: 12),
            Expanded( // Dùng Expanded để chữ tự xuống dòng nếu quá dài
              child: Text(
                message,
                style: const TextStyle(
                  color: Colors.black87,
                  fontWeight: FontWeight.w600,
                  fontSize: 13,
                ),
              ),
            ),
          ],
        ),
        backgroundColor: Colors.white.withOpacity(0.95),
        behavior: SnackBarBehavior.floating,
        elevation: 4,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(12),
          side: BorderSide(
              color: isError ? Colors.red.shade100 : Colors.blue.shade100,
              width: 1.5
          ),
        ),
        margin: const EdgeInsets.symmetric(horizontal: 50, vertical: 20),
        duration: const Duration(milliseconds: 1500), // 1.5s là vừa đủ đọc
      ),
    );
  }
// ── Serial Logic ────────────────────────
  void _refreshPorts({bool showMessage = false}) {
    setState(() {
      // 1. Ngắt kết nối vật lý và giải phóng tài nguyên
      if (_port != null && _port!.isOpen) {
        _port!.close();
        _port!.dispose(); // Giải phóng bộ nhớ của cổng
      }
      _isConnected = false;
      _isRunning = false;
      _availablePorts = SerialPort.availablePorts;
      if (_availablePorts.isNotEmpty && _selectedPort == null) {
        _selectedPort = _availablePorts.first;
      }
      if(showMessage)
      {
        _showCustomSnackBar("Port $_selectedPort has been closed");
      }
    });
  }

  void _connect() {
    if (_selectedPort == null) return;

    if (_port != null && _port!.isOpen) {
      _port!.close();
    }

    _port = SerialPort(_selectedPort!);
    if (!_port!.openReadWrite()) {
      _showCustomSnackBar("Error: Unable to open port $_selectedPort", isError: true);
      return;
    }

    SerialPortConfig config = _port!.config;
    config.baudRate = 9600;
    _port!.config = config;

    setState(() => _isConnected = true);
    _isRunning = false;

    // Bắt đầu phiên log mới cho PDF mỗi lần kết nối
    for (var l in sessionHistory) {
      l.clear();
    }
    _lastLogTime = null;
    _chartStartTime = null;
    _sessionExported = true;

    _showCustomSnackBar("Connected to port $_selectedPort");

    _reader = SerialPortReader(_port!); //theo dõi cong ket noi và tao ra event moi khi co du lieu duoc gui den
    _reader!.stream.listen((data) {
      _incomingBuffer += String.fromCharCodes(data);
      while (_incomingBuffer.contains('\n')) {
        int idx = _incomingBuffer.indexOf('\n');
        String completeLine = _incomingBuffer.substring(0, idx).trim();
        _incomingBuffer = _incomingBuffer.substring(idx + 1);

        if (completeLine.isNotEmpty) {
          debugPrint("BMS Packet: $completeLine");
          _parseBMSPacket(completeLine);
        }
      }
    });
  }

  void _toggleStartStop() async {
    if (_port == null || !_port!.isOpen) {
      _showCustomSnackBar("Please connect to a serial port before using Start/Stop", isError: true);
      return;
    }

    final bool nextState = !_isRunning;

    // ── Nhấn START nhưng dữ liệu phiên trước đó CHƯA được xuất PDF ──
    // -> tự động xuất PDF phiên cũ trước khi xóa để bắt đầu phiên mới
    if (nextState && !_sessionExported && sessionHistory.any((l) => l.isNotEmpty)) {
      _showCustomSnackBar("Previous session exported to PDF...");
      await _exportSessionToPdf(auto: true);
    }

    final String cmd = nextState ? '1' : '0';

    try {
      _port!.write(Uint8List.fromList(cmd.codeUnits));
      setState(() {
        _isRunning = nextState;

        // Khi nhấn START: reset đồ thị + bắt đầu ghi log mới cho PDF.
        // Khi nhấn STOP: chỉ dừng ghi (điều kiện _isRunning trong _parseBMSPacket),
        // dữ liệu vẫn được giữ nguyên trong sessionHistory để chờ xuất PDF thủ công.
        if (nextState) {
          for (var l in socTimeHistory) {
            l.clear();
          }
          for (int i = 0; i < _chartFinished.length; i++) {
            _chartFinished[i] = false;
          }
          _chartStartTime = DateTime.now();

          for (var l in sessionHistory) {
            l.clear();
          }
          _lastLogTime = null;
          _sessionExported = false;
        }
      });
      _showCustomSnackBar(nextState
          ? "START command sent — Data logging started"
          : "STOP command sent — Data logging stopped");
    } catch (e) {
      _showCustomSnackBar("Failed to send command: $e", isError: true);
    }
  }

  // ── Xuất dữ liệu ghi hiện tại ra file PDF ──
  // auto = true: gọi tự động (khi quên bấm xuất PDF trước khi START lại), không cần báo lỗi nếu rỗng
  Future<void> _exportSessionToPdf({bool auto = false}) async {
    if (sessionHistory.every((l) => l.isEmpty)) {
      if (!auto) {
        _showCustomSnackBar("No data available for PDF export", isError: true);
      }
      return;
    }

    // final String timestamp =
    //     DateTime.now().toIso8601String().replaceAll(':', '-').split('.').first;

    await exportHistoryToPdf(
      sessionHistory,
      fileName: 'Data_BMS_Charge.pdf',
    );

    _sessionExported = true;
    if (!auto) {
      _showCustomSnackBar("PDF exported successfully");
    }
  }

  void _parseBMSPacket(String rawLine) {
    try {
      // 1. Loại bỏ phần chữ "BMS Full Packet: Data:" để lấy chuỗi số
      if (!rawLine.contains("Data:")) return;
      String dataPart = rawLine.split("Data:").last.trim();

      // 2. Tách thành mảng các chuỗi số (Loại bỏ dấu chấm cuối cùng nếu có)
      if (dataPart.endsWith('.')) {
        dataPart = dataPart.substring(0, dataPart.length - 1);
      }
      List<String> rawValues = dataPart.split(',');

      // Kiểm tra nếu đủ ít nhất 31 giá trị (6 cell * 4 thông số + 1 dòng điện tổng của pack)
      if (rawValues.length >= 32) {
        List<double> voltages = [];
        List<double> currents = [];
        List<double> capacitys = [];
        List<double> socs = [];
        List<double> temps = [];

        for (int i = 0; i < 6; i++) {
          // Lưu ý: Chia tỷ lệ nếu dữ liệu gửi dạng số nguyên (VD: 2850 -> 2.85V)
          // Ở đây tôi chia cho 1000 cho Voltage và 1000 cho Current để chuyển đơn vị về V và A
          double v = (double.tryParse(rawValues[i]) ?? 0.0) / 1000.0;
          voltages.add(v);
          double c = (double.tryParse(rawValues[i + 6]) ?? 0.0) / 1000.0;
          currents.add(c);
          double cap = (double.tryParse(rawValues[i + 12]) ?? 0.0);
          capacitys.add(cap);
          socs.add(double.tryParse(rawValues[i + 18]) ?? 0.0);
          temps.add(double.tryParse(rawValues[i + 24]) ?? 0.0);
        }

        // Giá trị "current" tổng của pack, nằm ở cuối gói tin (phần tử thứ 31, index 30)
        // Cũng chia cho 1000 để chuyển từ mA sang A, giống các dòng điện cell khác
        final double packCurrentValue = (double.tryParse(rawValues[30]) ?? 0.0) / 1000.0;

        // Cờ trạng thái sạc/xả do thiết bị gửi trực tiếp: '1' = đang sạc, '0' = đang xả
        final String chargeFlagValue = rawValues[31].trim();

        // Mốc thời gian dùng chung cho cả điểm vẽ đồ thị và log PDF của gói tin này
        final now = DateTime.now();

        // 3. Cập nhật vào State của App
        setState(() {
          for (int i = 0; i < 6; i++) {
            cells[i] = cells[i].copyWith(
              voltage: voltages[i],
              current: currents[i],
              capacity: capacitys[i],
              soc: socs[i],
              temperature: temps[i],
              // capacity giữ nguyên hoặc tính toán thêm
            );
          }
          packCurrent = packCurrentValue;

          // Cờ trạng thái sạc/xả hiển thị trên dashboard (pin, badge...)
          chargeFlag = chargeFlagValue;

          // Nếu đây là lần đầu tiên có dữ liệu (chưa từng có mốc x=0), lấy luôn thời điểm hiện tại làm gốc
          _chartStartTime ??= now;

          for (int i = 0; i < 6; i++) {
            // Lưu điểm (Thời gian, SOC) vào lịch sử để vẽ biểu đồ,
            // CHỈ vẽ khi đang chạy (đã nhấn START), dừng lại khi nhấn STOP,
            // hoặc khi cell đã đạt SOC 100% (đồ thị giữ nguyên trạng thái cuối)
            if (_isRunning && !_chartFinished[i]) {
              final double elapsedSeconds =
                  now.difference(_chartStartTime!).inMilliseconds / 1000.0;
              socTimeHistory[i].add(FlSpot(elapsedSeconds, socs[i]));
              if (socTimeHistory[i].length > _maxHistoryPoints) {
                socTimeHistory[i].removeAt(0);
              }
              if (socs[i] >= 100) {
                _chartFinished[i] = true;
              }
            }
          }
        });

        // Ghi log lịch sử phục vụ xuất PDF, chỉ mỗi 1 phút/lần,
        // CHỈ ghi trong lúc đang chạy (giữa lúc nhấn START và STOP).
        // (đồ thị SOC ở trên vẫn cập nhật liên tục theo mọi gói tin, không bị ảnh hưởng)
        if (_isRunning &&
            (_lastLogTime == null || now.difference(_lastLogTime!) >= _logInterval)) {
          _lastLogTime = now;
          for (int i = 0; i < 6; i++) {
            sessionHistory[i].add({
              'time': now,
              'voltage': voltages[i],
              'current': currents[i],
              'soc': socs[i],
              'temperature': temps[i],
              'capacity': capacitys[i],
            });
          }
        }
      }
    } catch (e) {
      debugPrint("Packet parsing error: $e");
    }
  }

  @override
  void dispose() {
    _waveController.dispose();
    _port?.close();
    super.dispose();
  }

  // ── Summary values ─────────────────────
  //Việc sử dụng từ khóa get mục đích là để đảm bảo mỗi lần lấy dữ liệu đều là dữ liệu mới nhất.
  double get avgSoc => cells.map((c) => c.soc).reduce((a, b) => a + b) / cells.length;
  double get avgQ => cells.map((c) => c.capacity).reduce((a, b) => a + b) / cells.length;
  double get totalVoltage => cells.map((c) => c.voltage).reduce((a, b) => a + b);
  double get maxTemperature => cells.map((c) => c.temperature).reduce((a, b) => a > b ? a : b);

  bool get isCharging => chargeFlag == '1';

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Row(
        children: [
          // ── SIDEBAR ───────────────────
          _buildSidebar(),
          // ── CONTENT ───────────────────
          Expanded(
            child: IndexedStack(
              index: _selectedIndex,
              children: [DashboardPage(
                cells: cells,
                avgSoc: avgSoc,
                avgQ: avgQ,
                totalVoltage: totalVoltage,
                packCurrent: packCurrent,
                maxTemperature: maxTemperature,
                waveController: _waveController,
                isCharging: isCharging,
              ),ChartPage(
                socTimeHistory: socTimeHistory,
              )],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSidebar() {
    return Container(
      width: 210,
      color: Colors.white,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start, //căn chỉnh các widget sát lề trái
        children: [
          const SizedBox(height: 20),
          // Logo
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 10),
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              decoration: BoxDecoration(
                color: Colors.deepPurple.shade50.withOpacity(0.5), // Nền tím nhạt soft
                borderRadius: BorderRadius.circular(12),           // Bo góc mềm mại
                border: Border.all(
                  color: Colors.deepPurple.shade100,              // Viền mỏng đồng điệu với app
                  width: 1.5,
                ),
              ),
              child: Row(
                mainAxisSize: MainAxisSize.max, // Giãn đều theo chiều ngang khung chứa
                children: [
                  Icon(
                    Icons.battery_charging_full,
                    color: Colors.deepPurple.shade700,
                    size: 18, // Tăng nhẹ size icon cho cân đối
                  ),
                  const SizedBox(width: 10),
                  Text(
                    'BMS Monitor',
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w800, // Đậm nét hơn để làm nổi bật thương hiệu
                      color: Colors.deepPurple.shade700,
                      letterSpacing: 0.5,
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 20),
          //Widget để tạo các nút nhấn điều hướng
          _buildNavItem(icon: Icons.dashboard_rounded, label: 'Dashboard', index: 0),
          _buildNavItem(icon: Icons.stacked_line_chart_rounded, label: 'Graph', index: 1),
          const Spacer(), //Dùng để kéo giãn khoảng cách

          // ── KẾT NỐI SERIAL ───────────────────
          _buildSerialPanel(),

          // ── START/STOP ───────────────────
          _buildStartStopButton(),

          // ── XUẤT PDF ───────────────────
          _buildExportPdfButton(),

          Padding(
            padding: const EdgeInsets.all(16),
            child: Text('v1.0.0 · BMS Desktop',
                style: TextStyle(fontSize: 10, color: Colors.grey.shade400)),
          ),
        ],
      ),
    );
  }

  Widget _buildStartStopButton() {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 250),
        child: ElevatedButton.icon(
          onPressed: _toggleStartStop,
          icon: Icon(
            _isRunning ? Icons.stop_rounded : Icons.play_arrow_rounded,
            size: 20,
          ),
          label: Text(
            _isRunning ? 'STOP' : 'START',
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.bold,
              letterSpacing: 0.8,
            ),
          ),
          style: ElevatedButton.styleFrom(
            backgroundColor:
            _isRunning ? Colors.red.shade500 : Colors.green.shade600,
            foregroundColor: Colors.white,
            elevation: 0,
            minimumSize: const Size(double.infinity, 44),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
          ),
        ),
      ),
    );
  }

  Widget _buildExportPdfButton() {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
      child: ElevatedButton.icon(
        onPressed: () async {
          await _exportSessionToPdf();
        },
        icon: const Icon(Icons.picture_as_pdf, size: 16),
        label: const Text('EXPORT PDF',
            style: TextStyle(fontSize: 11, fontWeight: FontWeight.bold)),
        style: ElevatedButton.styleFrom(
          backgroundColor: Colors.deepPurple.shade500,
          foregroundColor: Colors.white,
          elevation: 0,
          minimumSize: const Size(double.infinity, 40),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
        ),
      ),
    );
  }

  Widget _buildSerialPanel() {
    return Container(
      padding: const EdgeInsets.all(16),
      margin: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: const Color(0xFFF8F9FE),
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: Colors.grey.shade200),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('SERIAL CONNECTION', style: TextStyle(fontSize: 10, fontWeight: FontWeight.w800, color: Colors.grey.shade500, letterSpacing: 0.5)),
          const SizedBox(height: 12),

          // Dropdown
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: BoxDecoration(
              color: Colors.white,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: Colors.grey.shade300),
            ),
            child: DropdownButtonHideUnderline(
              child: DropdownButton<String>(
                isExpanded: true,
                value: _availablePorts.contains(_selectedPort) ? _selectedPort : null,
                style: const TextStyle(fontSize: 12, color: Colors.black, fontWeight: FontWeight.w600),
                items: _availablePorts.toSet().map((name) => DropdownMenuItem(value: name, child: Text(name))).toList(),
                onChanged: (val) => setState(() => _selectedPort = val),
              ),
            ),
          ),

          const SizedBox(height: 12),

          Row(
            children: [
              Expanded(
                child: ElevatedButton(
                  onPressed: _connect,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: _isConnected ? Colors.green.shade600 : Colors.deepPurple.shade500,
                    foregroundColor: Colors.white,
                    elevation: 0,
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                    padding: const EdgeInsets.symmetric(vertical: 12),
                  ),
                  child: Text(_isConnected ? 'CONNECTED' : 'CONNECT', style: const TextStyle(fontSize: 11, fontWeight: FontWeight.bold)),
                ),
              ),
              const SizedBox(width: 8),
              IconButton(
                onPressed: () => _refreshPorts(showMessage: true),
                icon: const Icon(Icons.refresh, size: 18),
                style: IconButton.styleFrom(
                  backgroundColor: Colors.white,
                  side: BorderSide(color: Colors.grey.shade300),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildNavItem({required IconData icon, required String label, required int index}) {
    final isActive = _selectedIndex == index;
    //GestureDetector là widget dùng để nhận tương tác từ nguời dùng
    return GestureDetector(
      onTap: () => setState(() => _selectedIndex = index),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 200),
        margin: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: isActive ? const Color(0xFFEDE7F6) : Colors.transparent,
          borderRadius: BorderRadius.circular(10),
        ),
        child: Row(
          children: [
            Icon(icon, size: 18, color: isActive ? Colors.deepPurple.shade700 : Colors.grey.shade600),
            const SizedBox(width: 10),
            Text(label,
                style: TextStyle(
                  fontSize: 13,
                  fontWeight: isActive ? FontWeight.w600 : FontWeight.normal,
                  color: isActive ? Colors.deepPurple.shade700 : Colors.grey.shade600,
                )),
          ],
        ),
      ),
    );
  }
}

// ─────────────────────────────────────────
// DASHBOARD PAGE
// ─────────────────────────────────────────
class DashboardPage extends StatelessWidget {
  final List<CellData> cells;
  final double avgSoc;
  final double avgQ;
  final double totalVoltage;
  final double packCurrent;
  final double maxTemperature;
  final AnimationController waveController;
  final bool isCharging;

  const DashboardPage({
    super.key,
    required this.cells,
    required this.avgSoc,
    required this.avgQ,
    required this.totalVoltage,
    required this.packCurrent,
    required this.maxTemperature,
    required this.waveController,
    required this.isCharging,
  });

  @override
  Widget build(BuildContext context) {
    final double screenHeight = MediaQuery.of(context).size.height;
    final double topSectionHeight = screenHeight / 3;
    return Padding(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // ── TOP CARD ─────────────────
          SizedBox(
            height: topSectionHeight,
            child: _buildTopCard(context),
          ),
          const SizedBox(height: 14),
          // ── CELLS LABEL ───────────────
          Text('Cell Parameters',
              style: TextStyle(fontSize: 13, color: Colors.grey.shade600)),
          const SizedBox(height: 8),
          // ── CELLS GRID ────────────────
          Expanded(child: _buildCellsGrid()),
        ],
      ),
    );
  }

  Widget _buildTopCard(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        boxShadow: [BoxShadow(color: Colors.black.withOpacity(0.04), blurRadius: 8, offset: const Offset(0, 2))],
      ),
      child: Row(
        children: [
          // Pin tổng
          _buildBatterySection(),
          const SizedBox(width: 140),
          // Divider
          Container(width: 1, height: 90, color: const Color(0xFFF0F0F0)),
          const SizedBox(width: 20),
          // Sensor cards
          Expanded(child: _buildSensorGrid()),
        ],
      ),
    );
  }

  Widget _buildBatterySection() {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          width: 64,
          height: 108,
          //AnimatedBuilder là widget dùng để xây dựng hiệu ứng chuyển động.
          child: AnimatedBuilder(
            animation: waveController,
            builder: (_, __) => CustomPaint(  // dùng dấu gạch để bỏ qua các thuộc tính không dùng đến
              painter: BatteryPainter(
                level: avgSoc / 100,
                wavePhase: waveController.value * 2 * pi,
                isCharging: isCharging,
              ),
            ),
          ),
        ),
        const SizedBox(width: 14),
        Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          mainAxisSize: MainAxisSize.min,
          children: [
            Text('System Status',
                style: TextStyle(fontSize: 11, color: Colors.grey.shade400)),
            const SizedBox(height: 4),
            Text('${avgSoc.toStringAsFixed(0)}%',
                style: TextStyle(fontSize: 32, fontWeight: FontWeight.w700,
                    color: Colors.deepPurple.shade800, height: 1)),
            const SizedBox(height: 8),
            _pill(
                color: isCharging ? Colors.orange : Colors.blue,
                label: isCharging ? 'Charging' : 'Discharging'
            ),
          ],
        ),
      ],
    );
  }

  Widget _pill({required Color color, required String label}) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
      decoration: BoxDecoration(
        color: Colors.grey.shade100,
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(width: 8, height: 8, decoration: BoxDecoration(color: color, shape: BoxShape.circle)),
          const SizedBox(width: 6),
          Text(label, style: const TextStyle(fontSize: 11, color: Color(0xFF555555))),
        ],
      ),
    );
  }

  Widget _buildSensorGrid() {
    //LayoutBuilder là widget cho phép điều chỉnh không gian sao cho đẹp nhất khi biết chiều rộng và cao
    return LayoutBuilder(
      builder: (context, constraints) {
        // 1. Tính toán chiều rộng thực tế của 1 ô (trừ đi khoảng cách giữa 2 cột)
        final double itemWidth = (constraints.maxWidth - 10) / 2;

        // 2. Tính toán chiều cao thực tế của 1 ô (trừ đi khoảng cách giữa 2 hàng)
        // constraints.maxHeight lúc này đã được giới hạn bởi SizedBox (1/4 màn hình) ở hàm build
        final double itemHeight = (constraints.maxHeight - 10) / 2;

        // 3. Tính tỷ lệ AspectRatio chính xác
        // Nếu itemHeight <= 0 (do khung quá nhỏ), mặc định trả về tỷ lệ an toàn 2.0
        final double aspectRatio = itemHeight > 0 ? (itemWidth / itemHeight) : 2.0;

        return GridView.count(
          shrinkWrap: true, //thu nhỏ kích thước vừa đủ với nội dung
          crossAxisCount: 2,  // tạo thành 2 cột
          childAspectRatio: aspectRatio,
          mainAxisSpacing: 10,
          crossAxisSpacing: 10,
          physics: const NeverScrollableScrollPhysics(),
          children: [
            _sensorCard(label: 'Temperature', value: maxTemperature.toStringAsFixed(0), unit: '°C', color: Colors.deepOrange.shade300),
            _sensorCard(label: 'Capacity', value: avgQ.toStringAsFixed(0), unit: 'mAh', color: const Color(0xFF10B981)),
            _sensorCard(label: 'Voltage', value: totalVoltage.toStringAsFixed(2), unit: 'V', color: Colors.deepPurple.shade800),
            _sensorCard(label: 'Charging Current', value: packCurrent.toStringAsFixed(3), unit: 'A', color: const Color(0xFFF59E0B)),
          ],
        );
      },
    );
  }

  Widget _sensorCard({required String label, required String value, required String unit, required Color color}) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
      decoration: BoxDecoration(
        color: const Color(0xFFFAFAFA),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Text(label, style: TextStyle(fontSize: 11, color: Color(0xFF94A3B8))),
          const SizedBox(height: 4),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            //Đảm bảo cho các chữ có kích thước khác nhau cùng nằm trên một đường thẳng
            children: [
              Text(value, style: TextStyle(fontSize: 20, fontWeight: FontWeight.w700, color: color)),
              if (unit.isNotEmpty) ...[ //dấu ... cho phép thêm được nhiều widget để cùng thỏa mãn điều kiện này.
                const SizedBox(width: 3),
                Text(unit, style: TextStyle(fontSize: 13, fontWeight: FontWeight.w500, color: Colors.grey.shade400)),
              ],
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildCellsGrid() {
    return LayoutBuilder(
      builder: (context, constraints) {
        // 1. Cấu hình 3 cột
        const int crossAxisCount = 3;
        const double spacing = 10;

        // 2. Tính chiều rộng mỗi ô: (Tổng rộng - các khoảng cách giữa cột) / 3
        final double itemWidth = (constraints.maxWidth - (spacing * (crossAxisCount - 1))) / crossAxisCount;

        // 3. Tính chiều cao mỗi ô: (Tổng cao - khoảng cách giữa 2 hàng) / 2
        final double itemHeight = (constraints.maxHeight - spacing) / 2;

        // 4. Tỷ lệ vàng để Grid khít khung
        final double aspectRatio = itemWidth / itemHeight;

        return GridView.builder(
          physics: const NeverScrollableScrollPhysics(), // Tắt cuộn để nằm gọn trong dashboard
          gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: crossAxisCount,
            crossAxisSpacing: spacing,
            mainAxisSpacing: spacing,
            childAspectRatio: aspectRatio > 0 ? aspectRatio : 2.8,
          ),
          itemCount: cells.length,
          itemBuilder: (_, i) => CellCard(cell: cells[i], index: i, isCharging: isCharging), //dùng để đưa dữ liệu của các cell pin vào từng ô tương ứng
        );
      },
    );
  }
}

// ─────────────────────────────────────────
// CHART PAGE — Đồ thị SOC theo Thời gian
// ─────────────────────────────────────────
class ChartPage extends StatelessWidget {
  final List<List<FlSpot>> socTimeHistory;

  const ChartPage({super.key, required this.socTimeHistory});

  // Mỗi cell có 1 màu riêng để phân biệt 6 đường trên biểu đồ
  static const List<Color> _cellColors = [
    Color(0xFF7B1FA2), // Cell 1 - tím
    Color(0xFF1565C0), // Cell 2 - xanh dương
    Color(0xFF2E7D32), // Cell 3 - xanh lá
    Color(0xFFE65100), // Cell 4 - cam
    Color(0xFFC62828), // Cell 5 - đỏ
    Color(0xFF00838F), // Cell 6 - xanh ngọc
  ];

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text('SOC vs Time Graph',
              style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                  color: Colors.deepPurple.shade700)),
          const SizedBox(height: 4),
          Text('X-axis: Time (mm:ss) · Y-axis: SOC (%) · Each graph represents one battery cell',
              style: TextStyle(fontSize: 12, color: Colors.grey.shade500)),
          const SizedBox(height: 16),
          Expanded(child: _buildChartsGrid()),
        ],
      ),
    );
  }

  Widget _buildChartsGrid() {
    return LayoutBuilder(
      builder: (context, constraints) {
        const int crossAxisCount = 3;
        const double spacing = 14;

        final double itemWidth =
            (constraints.maxWidth - (spacing * (crossAxisCount - 1))) / crossAxisCount;
        final double itemHeight = (constraints.maxHeight - spacing) / 2;
        final double aspectRatio = itemWidth / itemHeight;

        return GridView.builder(
          physics: const NeverScrollableScrollPhysics(),
          gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: crossAxisCount,
            crossAxisSpacing: spacing,
            mainAxisSpacing: spacing,
            childAspectRatio: aspectRatio > 0 ? aspectRatio : 1.4,
          ),
          itemCount: 6,
          itemBuilder: (_, i) => _buildSingleChartCard(i),
        );
      },
    );
  }

  Widget _buildSingleChartCard(int index) {
    final color = _cellColors[index];
    return Container(
      padding: const EdgeInsets.fromLTRB(6, 14, 14, 10),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        boxShadow: [
          BoxShadow(
              color: Colors.black.withOpacity(0.04),
              blurRadius: 8,
              offset: const Offset(0, 2))
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 9,
                height: 9,
                margin: const EdgeInsets.only(left: 6),
                decoration: BoxDecoration(color: color, shape: BoxShape.circle),
              ),
              const SizedBox(width: 6),
              Text('Cell ${index + 1}',
                  style: const TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: Color(0xFF555555))),
            ],
          ),
          const SizedBox(height: 6),
          Expanded(child: _buildSingleChart(index)),
        ],
      ),
    );
  }

  Widget _buildSingleChart(int index) {
    final color = _cellColors[index];
    return _CellMiniChart(color: color, spots: socTimeHistory[index]);
  }
}

// ─────────────────────────────────────────
// CELL MINI CHART — đồ thị SOC theo thời gian cho 1 cell, panel thông số
// bám theo góc dưới-phải của chấm tròn khi rê chuột
// ─────────────────────────────────────────
class _CellMiniChart extends StatefulWidget {
  final Color color;
  final List<FlSpot> spots;

  const _CellMiniChart({required this.color, required this.spots});

  @override
  State<_CellMiniChart> createState() => _CellMiniChartState();
}

class _CellMiniChartState extends State<_CellMiniChart> {
  double? _hoverTime;
  double? _hoverSoc;

  // Vùng vẽ thực tế của đồ thị (trừ đi phần trục số bên trái/dưới)
  static const double _leftAxisWidth = 34;
  static const double _bottomAxisHeight = 18;
  static const double _minY = 0, _maxY = 100;
  // Khoảng thời gian tối thiểu hiển thị trên trục hoành khi chưa có nhiều dữ liệu (60 giây)
  static const double _minTimeSpan = 60;

  // Định dạng số giây thành chuỗi mm:ss (hoặc h:mm:ss nếu vượt quá 1 giờ)
  static String _formatTime(double totalSeconds) {
    final int secs = totalSeconds.round();
    final int h = secs ~/ 3600;
    final int m = (secs % 3600) ~/ 60;
    final int s = secs % 60;
    if (h > 0) {
      return '${h}:${m.toString().padLeft(2, '0')}:${s.toString().padLeft(2, '0')}';
    }
    return '${m}:${s.toString().padLeft(2, '0')}';
  }

  @override
  Widget build(BuildContext context) {
    // Tính khoảng giá trị trục X dựa trên dữ liệu hiện có
    final double maxX = widget.spots.isEmpty
        ? _minTimeSpan
        : (widget.spots.last.x < _minTimeSpan ? _minTimeSpan : widget.spots.last.x);
    const double minX = 0;
    // Chọn bước chia trục X hợp lý theo độ dài chuỗi thời gian
    final double xInterval = maxX <= 120
        ? 20
        : maxX <= 600
        ? 60
        : maxX <= 3600
        ? 300
        : 900;

    return LayoutBuilder(
      builder: (context, constraints) {
        final double plotWidth = constraints.maxWidth - _leftAxisWidth;
        final double plotHeight = constraints.maxHeight - _bottomAxisHeight;

        Offset? dotPos;
        if (_hoverTime != null && _hoverSoc != null && plotWidth > 0 && plotHeight > 0) {
          final double px = _leftAxisWidth + (_hoverTime! - minX) / (maxX - minX) * plotWidth;
          final double py = (1 - (_hoverSoc! - _minY) / (_maxY - _minY)) * plotHeight;
          dotPos = Offset(px, py);
        }

        return Stack(
          clipBehavior: Clip.none,
          children: [
            MouseRegion(
              onExit: (_) {
                if (_hoverTime != null) {
                  setState(() {
                    _hoverTime = null;
                    _hoverSoc = null;
                  });
                }
              },
              child: LineChart(
                LineChartData(
                  minX: minX,
                  maxX: maxX,
                  minY: _minY,
                  maxY: _maxY,
                  gridData: FlGridData(
                    show: true,
                    drawVerticalLine: true,
                    verticalInterval: xInterval,
                    horizontalInterval: 20,
                    getDrawingHorizontalLine: (value) => FlLine(
                        color: const Color(0xFFEFEFEF), strokeWidth: 1, dashArray: [4, 4]),
                    getDrawingVerticalLine: (value) => FlLine(
                        color: const Color(0xFFEFEFEF), strokeWidth: 1, dashArray: [4, 4]),
                  ),
                  borderData: FlBorderData(
                    show: true,
                    border: Border.all(color: const Color(0xFFBDBDBD), width: 1.2),
                  ),
                  titlesData: FlTitlesData(
                    show: true,
                    topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                    rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
                    bottomTitles: AxisTitles(
                      sideTitles: SideTitles(
                        showTitles: true,
                        interval: xInterval,
                        reservedSize: _bottomAxisHeight,
                        getTitlesWidget: (value, meta) => Padding(
                          padding: const EdgeInsets.only(top: 4),
                          child: Text(_formatTime(value),
                              style: TextStyle(fontSize: 9, color: Colors.grey.shade500)),
                        ),
                      ),
                    ),
                    leftTitles: AxisTitles(
                      sideTitles: SideTitles(
                        showTitles: true,
                        interval: 20,
                        reservedSize: _leftAxisWidth,
                        getTitlesWidget: (value, meta) {
                          if (value < 0.01) return const SizedBox();
                          return Align(
                            alignment: Alignment.centerRight,
                            child: Padding(
                              padding: const EdgeInsets.only(right: 4),
                              child: Text('${value.toInt()}%',
                                  maxLines: 1,
                                  softWrap: false,
                                  overflow: TextOverflow.visible,
                                  style: TextStyle(fontSize: 9, color: Colors.grey.shade500)),
                            ),
                          );
                        },
                      ),
                    ),
                  ),
                  lineTouchData: LineTouchData(
                    // Ẩn tooltip mặc định, thay bằng panel tự vẽ bên dưới
                    touchTooltipData: LineTouchTooltipData(
                      getTooltipColor: (touchedSpot) => Colors.transparent,
                      tooltipPadding: EdgeInsets.zero,
                      tooltipMargin: 0,
                      getTooltipItems: (touchedSpots) =>
                          touchedSpots.map((e) => null).toList(),
                    ),
                    touchCallback: (event, response) {
                      if (response == null ||
                          response.lineBarSpots == null ||
                          response.lineBarSpots!.isEmpty) {
                        if (_hoverTime != null) {
                          setState(() {
                            _hoverTime = null;
                            _hoverSoc = null;
                          });
                        }
                        return;
                      }
                      final spot = response.lineBarSpots!.first;
                      setState(() {
                        _hoverTime = spot.x;
                        _hoverSoc = spot.y;
                      });
                    },
                    getTouchedSpotIndicator: (barData, spotIndexes) {
                      return spotIndexes.map((index) {
                        return TouchedSpotIndicatorData(
                          FlLine(color: widget.color.withOpacity(0.3), strokeWidth: 1),
                          FlDotData(
                            show: true,
                            getDotPainter: (spot, percent, bar, idx) => FlDotCirclePainter(
                              radius: 3,
                              color: widget.color,
                              strokeWidth: 0,
                            ),
                          ),
                        );
                      }).toList();
                    },
                  ),
                  lineBarsData: [
                    LineChartBarData(
                      spots: widget.spots,
                      isCurved: false,
                      color: widget.color,
                      barWidth: 2,
                      dotData: const FlDotData(show: false),
                      belowBarData: BarAreaData(show: false),
                    ),
                  ],
                ),
              ),
            ),
            if (dotPos != null)
              Positioned(
                left: (dotPos.dx + 6)
                    .clamp(0, (constraints.maxWidth - 74).clamp(0, double.infinity)),
                top: (dotPos.dy + 6)
                    .clamp(0, (constraints.maxHeight - 36).clamp(0, double.infinity)),
                child: IgnorePointer(
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 5),
                    decoration: BoxDecoration(
                      color: const Color(0xFFF1F5F9),
                      borderRadius: BorderRadius.circular(8),
                      border: Border.all(color: widget.color.withOpacity(0.35), width: 1),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(_formatTime(_hoverTime!),
                            style: const TextStyle(
                                fontSize: 9,
                                fontWeight: FontWeight.w600,
                                color: Color(0xFF475569))),
                        Text('${_hoverSoc!.toStringAsFixed(0)}%',
                            style: TextStyle(
                                fontSize: 10,
                                fontWeight: FontWeight.w700,
                                color: widget.color)),
                      ],
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}


// ─────────────────────────────────────────
// CELL CARD
// ─────────────────────────────────────────
class CellCard extends StatelessWidget {
  final CellData cell;
  final int index;
  final bool isCharging;

  const CellCard({super.key, required this.cell, required this.index, required this.isCharging});

  Color get _statusBg {
    if (cell.soc >= 80) return const Color(0xFFE8F5E9);
    if (cell.soc >= 30) return const Color(0xFFFFF3E0);
    return const Color(0xFFFFEBEE);
  }

  String get _statusLabel {
    if (cell.soc >= 80) return 'Normal';
    if (cell.soc >= 30) return 'Mid';
    return 'Low';
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(14),
        boxShadow: [
          BoxShadow(color: Colors.black.withOpacity(0.03),
              blurRadius: 6,
              offset: const Offset(0, 2))
        ],
      ),
      child: Row(
        children: [
          // ── Left: mini battery + soc ──
          SizedBox(
            width: 45,
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                SizedBox(
                  width: 30,
                  height: 48,
                  child: CustomPaint(
                    painter: MiniBatteryPainter(
                      level: cell.soc / 100,
                      color: const Color(0xFF555555),
                      isCharging: isCharging,
                    ),
                  ),
                ),
                const SizedBox(height: 4),
                Text('${cell.soc.toStringAsFixed(0)}%',
                    style: TextStyle(fontSize: 11,
                        fontWeight: FontWeight.w700,
                        color: Colors.lightGreen.shade700)),
              ],
            ),
          ),
          const SizedBox(width: 10),
          // ── Mid: name + voltage + badge ──
          Expanded(
            flex: 4,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Text('CELL ${index + 1}',
                    style: const TextStyle(
                        fontSize: 10, fontWeight: FontWeight.w600,
                        color: Color(0xFFAAAAAA), letterSpacing: 0.5)),
                const SizedBox(height: 2),
                Row(
                  crossAxisAlignment: CrossAxisAlignment.baseline,
                  textBaseline: TextBaseline.alphabetic,
                  children: [
                    Text(cell.voltage.toStringAsFixed(2),
                        style: TextStyle(
                            fontSize: 24, fontWeight: FontWeight.w700,
                            color: Colors.deepPurple.shade800, height: 1)),
                    const SizedBox(width: 2),
                    Text('V', style: TextStyle(fontSize: 16,
                        fontWeight: FontWeight.w600,
                        color: Colors.deepPurple.shade800)),
                  ],
                ),
                const SizedBox(height: 4),
                Container(
                  padding: const EdgeInsets.symmetric(
                      horizontal: 8, vertical: 2),
                  decoration: BoxDecoration(
                    color: _statusBg,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(_statusLabel,
                      style: TextStyle(fontSize: 9,
                          fontWeight: FontWeight.w500,
                          color: Color(0xFF2E7D32))),
                ),
              ],
            ),
          ),
          // ── Right: metrics vertical ──
          Expanded(
            flex: 4,
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                _metricRow(label: 'Current',
                    value: '${cell.current.toStringAsFixed(3)} A',
                    color: const Color(0xFF555555)),
                const SizedBox(height: 5),
                _metricRow(label: 'Temperature',
                    value: '${cell.temperature.toStringAsFixed(0)} °C',
                    color: const Color(0xFF555555)),
                const SizedBox(height: 5),
                _metricRow(label: 'Capacity',
                    value: '${cell.capacity.toStringAsFixed(0)} mAh',
                    color: const Color(0xFF555555)),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _metricRow(
      {required String label, required String value, required Color color}) {
    return LayoutBuilder(
      builder: (context, constraints) {
        // Tính fontSize dựa trên chiều rộng của CHÍNH CÁI Ô XÁM (constraints.maxWidth)
        double labelSize = (constraints.maxWidth * 0.1).clamp(8.0, 12.0);
        double valueSize = (constraints.maxWidth * 0.12).clamp(8.0, 12.0);

        return Container(
          width: double.infinity,
          margin: const EdgeInsets.symmetric(vertical: 2),
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: BoxDecoration(
            color: const Color(0xFFF7F8FC),
            borderRadius: BorderRadius.circular(8),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(label, style: TextStyle(
                  fontSize: labelSize, color: Color(0xFFCCCCCC))),
              Text(value, style: TextStyle(
                  fontSize: valueSize,
                  fontWeight: FontWeight.w600,
                  color: color)),
            ],
          ),
        );
      },
    );
  }
}

// ─────────────────────────────────────────
// BATTERY PAINTER (main — with wave)
// ─────────────────────────────────────────
class BatteryPainter extends CustomPainter {
  final double level;
  final double wavePhase;
  final bool isCharging;

  BatteryPainter({required this.level, required this.wavePhase, required this.isCharging});

  @override
  void paint(Canvas canvas, Size size) {
    const r = 7.0;        //bán kính bo góc
    final bx = 3.0, by = 8.0; //tọa độ bắt đầu của thân pin
    final bw = size.width - 6, bh = size.height - 10; //chiều rộng và cao của thân pin

    final outline = Paint()
    //dấu . trước .color là một cách viết tắt đại diện cho outline
      ..color = const Color(0xFF555555)
      ..style = PaintingStyle.stroke  //chỉ vẽ nét viền
      ..strokeWidth = 2;              //độ dày nét vẽ 2px

    final rrect = RRect.fromLTRBR(bx, by, bx + bw, by + bh, const Radius.circular(r));
    canvas.drawRRect(rrect, outline); //vẽ hình chữ nhật bo góc

    // Vẽ núm pin
    final termW = 16.0, termH = 6.0;    //chiều rong và cao của num pin
    final termX = (size.width - termW) / 2; //tọa độ x bat dau ve num pin
    canvas.drawRRect(
      RRect.fromLTRBAndCorners(termX, by - termH, termX + termW, by,
          topLeft: const Radius.circular(3), topRight: const Radius.circular(3)),
      Paint()..color = const Color(0xFF555555),
    );

    // Fill with wave
    final fh = (bh - 5) * level.clamp(0, 1);  //tinh chieu cao muc nuoc dua vao level
    final fy = by + bh - 3 - fh;              //toa do Y cua mat nuoc

    canvas.save();
    canvas.clipRRect(RRect.fromLTRBR(bx + 2.5, by + 2.5, bx + bw - 2.5, by + bh - 2.5, const Radius.circular(5)));
    //giúp cho bat ky chi tiet ben trong pin neu co bi lo ra ngoai thi no se tu dong cat bo

    final path = Path();
    path.moveTo(bx + 2.5, by + bh - 2.5); //bắt đầu vẽ từ goc duoi ben trai
    //ve duong cong hinh sin cho mat nuoc
    for (double x = 0; x <= bw - 5; x++) {
      final y = fy - 4 * sin((x / (bw - 5)) * 2 * pi + wavePhase);
      x == 0 ? path.lineTo(bx + 2.5, y) : path.lineTo(bx + 2.5 + x, y);
    }
    path.lineTo(bx + bw - 2.5, by + bh - 2.5);
    path.close();

    canvas.drawPath(path, Paint()..color = const Color(0xCCFFC000));

    // Bolt icon text
    if(isCharging)
    {
      final tp = TextPainter(
        text: const TextSpan(text: '⚡', style: TextStyle(fontSize: 14)),
        textDirection: TextDirection.ltr,
      )..layout(); //xác định chieu rong và cao cua chu
      tp.paint(canvas, Offset(size.width / 2 - tp.width / 2, by + (bh / 2) - (tp.height / 2)));
    }

    canvas.restore(); //do su dung clipRRect de tao khung gioi han nen sau do can goi ham nay de go bo khung
  }

  //vẽ lại muc nuoc và song khi chung no thay doi
  @override
  bool shouldRepaint(BatteryPainter old) =>
      old.level != level || old.wavePhase != wavePhase;
}

// ─────────────────────────────────────────
// MINI BATTERY PAINTER (cell cards)
// ─────────────────────────────────────────
class MiniBatteryPainter extends CustomPainter {
  final double level;
  final Color color;
  final bool isCharging;

  MiniBatteryPainter({required this.level, required this.color, required this.isCharging});

  @override
  void paint(Canvas canvas, Size size) {
    const r = 5.0;
    final bx = 1.0, by = 6.0;
    final bw = size.width - 2, bh = size.height - 8;

    canvas.drawRRect(
      RRect.fromLTRBR(bx, by, bx + bw, by + bh, const Radius.circular(r)),
      Paint()
        ..color = color
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.8,
    );

    // Terminal
    final termW = 12.0, termH = 5.0;
    final termX = (size.width - termW) / 2;
    canvas.drawRRect(
      RRect.fromLTRBAndCorners(termX, by - termH, termX + termW, by,
          topLeft: const Radius.circular(2), topRight: const Radius.circular(2)),
      Paint()..color = color,
    );

    // Fill
    final fh = (bh - 4) * level.clamp(0, 1);
    final fy = by + bh - 2 - fh;

    canvas.save();
    canvas.clipRRect(RRect.fromLTRBR(bx + 2, by + 2, bx + bw - 2, by + bh - 2, const Radius.circular(3)));
    canvas.drawRect(Rect.fromLTWH(bx + 2, fy, bw - 4, fh), Paint()..color = const Color(0xCCFFC000));

    // Bolt
    if(isCharging)
    {
      final tp = TextPainter(
        text: TextSpan(text: '⚡', style: TextStyle(fontSize: 10, color: color)),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(size.width / 2 - tp.width / 2, by + (bh / 2) - tp.height / 2));
    }

    canvas.restore();
  }

  @override
  bool shouldRepaint(MiniBatteryPainter old) => old.level != level || old.color != color;
}