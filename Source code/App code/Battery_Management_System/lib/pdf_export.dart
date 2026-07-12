import 'dart:io';
import 'package:flutter/services.dart' show rootBundle; // Cần thiết để load font từ assets
import 'package:pdf/pdf.dart';
import 'package:pdf/widgets.dart' as pw;

/// Xuất file PDF từ dữ liệu lịch sử của 1 phiên (sạc hoặc xả).
///
/// [fullHistory] : dữ liệu 6 cell cần xuất (đã được lọc đúng theo phiên sạc/xả từ trước).
/// [fileName]    : tên file PDF sẽ được lưu, ví dụ 'Data_BMS_Sac.pdf' hoặc 'Data_BMS_Xa.pdf'.
Future<void> exportHistoryToPdf(
    List<List<Map<String, dynamic>>> fullHistory, {
      String fileName = 'Data_BMS.pdf',
    }) async {
  final pdf = pw.Document();

  final fontData = await rootBundle.load("assets/fonts/NotoSans-Regular.ttf");
  final fontBoldData = await rootBundle.load("assets/fonts/NotoSans-Bold.ttf");

  final ttfRegular = pw.Font.ttf(fontData);
  final ttfBold = pw.Font.ttf(fontBoldData);

  // Tạo một theme chung sử dụng font tiếng Việt để áp dụng cho toàn bộ văn bản (bao gồm cả bảng)
  final myTheme = pw.ThemeData.withFont(
    base: ttfRegular,
    bold: ttfBold,
  );

  for (int i = 0; i < fullHistory.length; i++) {
    final records = fullHistory[i];
    if (records.isEmpty) continue;

    pdf.addPage(
      pw.MultiPage(
        pageFormat: PdfPageFormat.a4,
        theme: myTheme, // Áp dụng font tiếng Việt vào trang này
        build: (context) {
          return [
            // 2. Căn giữa tiêu đề bằng pw.Center
            pw.Center(
              child: pw.Text(
                'Thông số của Cell ${i + 1}',
                style: pw.TextStyle(
                  font: ttfBold, // Sử dụng font bold tiếng Việt
                  fontSize: 16,
                ),
              ),
            ),
            pw.SizedBox(height: 15),

            pw.Table.fromTextArray(
              headerStyle: pw.TextStyle(font: ttfBold, fontSize: 9),
              cellStyle: pw.TextStyle(font: ttfRegular, fontSize: 8),
              headerDecoration: const pw.BoxDecoration(color: PdfColors.blue300),
              cellPadding: const pw.EdgeInsets.symmetric(horizontal: 4, vertical: 3),
              headers: ['Thời gian', 'Điện áp (V)', 'Dòng điện (A)', 'SOC (%)', 'Nhiệt độ (°C)', 'Dung lượng (mAh)'],
              data: records.map((r) {
                final t = r['time'] as DateTime;
                final hh = t.hour.toString().padLeft(2, '0');
                final mm = t.minute.toString().padLeft(2, '0');
                final ss = t.second.toString().padLeft(2, '0');
                return [
                  '$hh:$mm:$ss',
                  (r['voltage'] as double).toStringAsFixed(3),
                  (r['current'] as double).toStringAsFixed(3),
                  (r['soc'] as double).toStringAsFixed(0),
                  (r['temperature'] as double).toStringAsFixed(0),
                  (r['capacity'] as double).toStringAsFixed(0),
                ];
              }).toList(),
            ),
          ];
        },
      ),
    );
  }

  final file = File('D:/app/$fileName');
  await file.writeAsBytes(await pdf.save());
}