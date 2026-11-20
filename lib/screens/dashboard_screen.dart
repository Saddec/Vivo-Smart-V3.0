import 'dart:async';
import 'dart:io';
import 'package:dio/dio.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:file_picker/file_picker.dart';
import '../providers/app_state.dart';

class DashboardScreen extends ConsumerStatefulWidget {
  const DashboardScreen({super.key});

  @override
  ConsumerState<DashboardScreen> createState() => _DashboardScreenState();
}

String? _findMatchingFile(List<String> audioFiles, String? target) {
  if (target == null || target.isEmpty) return null;
  if (audioFiles.contains(target)) return target;
  final targetName = target.contains('/') ? target.split('/').last : target;
  for (final file in audioFiles) {
    final fileName = file.contains('/') ? file.split('/').last : file;
    if (fileName == targetName) {
      return file;
    }
  }
  return null;
}

class _DashboardScreenState extends ConsumerState<DashboardScreen> {
  int _bottomNavIndex = 0;
  int _prayersSubTabIndex = 0;
  int _controlSubTabIndex = 0;
  int _settingsSubTabIndex = 0;
  int _filesSubTabIndex = 0;
  int _alertsSubTabIndex = 0;
  double? _localVolume; // For responsive sliding before API completes
  bool _isDraggingVolume = false; // Drag-lock flag to prevent volume stuttering
  Timer? _volumeDebounceTimer;
  String _currentFolder = '/'; // Current SD folder path (flat list filtered by folder)

  // Quick player states
  String? _selectedQuickPlayerFile;

  // Eid mode local editing states
  String? _selectedEidTakbeerFile;
  int? _localEidTakbeerVolume;
  List<Map<String, dynamic>>? _localEidPrayers;

  // Maghrib alerts local editing states
  int? _localMaghribOffset;
  List<Map<String, dynamic>>? _localMaghribAlerts;

  // Prayer times source and manual adjustment state variables
  bool _prayerConfigInitialized = false;
  String? _selectedPrayerSource;
  int _hijriOffset = 0;
  String _calcMethod = 'UmmAlQura';
  final Map<String, TextEditingController> _manualPrayerControllers = {
    'fajr': TextEditingController(text: '05:00'),
    'dhuhr': TextEditingController(text: '12:00'),
    'asr': TextEditingController(text: '15:30'),
    'maghrib': TextEditingController(text: '18:45'),
    'isha': TextEditingController(text: '20:15'),
  };

  // Location selection states
  List<String> _countries = [];
  List<String> _cities = [];
  String? _selectedCountry;
  String? _selectedCity;
  bool _loadingLocation = false;

  static const List<String> _daysOfWeek = ['الأحد', 'الإثنين', 'الثلاثاء', 'الأربعاء', 'الخميس', 'الجمعة', 'السبت'];
  static const List<String> _prayerNames = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];



  String _translatePrayerSource(String source) {
    final src = source.toLowerCase().trim();
    if (src == 'internet' || src == 'انترنت') {
      return 'انترنت';
    } else if (src == 'manual' || src == 'يدوي') {
      return 'يدوي';
    } else {
      return 'روزمانة';
    }
  }
  String _getDayName(String dateStr) {
    if (dateStr.isEmpty) return '';
    try {
      final parsed = DateTime.parse(dateStr);
      final weekday = parsed.weekday; // 1 is Monday, 7 is Sunday
      const arDays = {
        1: 'الإثنين',
        2: 'الثلاثاء',
        3: 'الأربعاء',
        4: 'الخميس',
        5: 'الجمعة',
        6: 'السبت',
        7: 'الأحد',
      };
      return arDays[weekday] ?? '';
    } catch (_) {
      return '';
    }
  }

  String _formatTime(String timeStr, bool is12Hour) {
    if (timeStr.isEmpty || !timeStr.contains(':')) return timeStr;
    if (!is12Hour) return timeStr;
    try {
      final parts = timeStr.split(':');
      final hour = int.parse(parts[0]);
      final minute = int.parse(parts[1]);
      String secStr = '';
      if (parts.length > 2) {
        secStr = ':${parts[2]}';
      }
      final period = hour >= 12 ? 'م' : 'ص';
      final h12 = hour % 12 == 0 ? 12 : hour % 12;
      final minStr = minute.toString().padLeft(2, '0');
      final hourStr = h12.toString().padLeft(2, '0');
      return '$hourStr:$minStr$secStr $period';
    } catch (_) {
      return timeStr;
    }
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _initLocationData();
    });
  }

  Future<void> _initLocationData() async {
    setState(() {
      _loadingLocation = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final config = await api.getPrayerConfig();
      final savedCountry = config['country'] ?? 'Saudi Arabia';
      final savedCity = config['city'] ?? 'Makkah';

      final countriesList = await api.getCountries();
      final citiesList = await api.getCities(savedCountry);

      setState(() {
        _countries = countriesList;
        _cities = citiesList;
        _selectedCountry = countriesList.contains(savedCountry) ? savedCountry : (countriesList.isNotEmpty ? countriesList.first : savedCountry);
        _selectedCity = citiesList.contains(savedCity) ? savedCity : (citiesList.isNotEmpty ? citiesList.first : savedCity);
      });
    } catch (e) {
      debugPrint('Error initializing location data: $e');
    } finally {
      if (mounted) {
        setState(() {
          _loadingLocation = false;
        });
      }
    }
  }

  Future<void> _onCountryChanged(String? newCountry) async {
    if (newCountry == null || newCountry == _selectedCountry) return;
    setState(() {
      _selectedCountry = newCountry;
      _cities = [];
      _selectedCity = null;
      _loadingLocation = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final citiesList = await api.getCities(newCountry);
      setState(() {
        _cities = citiesList;
        if (citiesList.isNotEmpty) {
          _selectedCity = citiesList.first;
        }
      });
    } catch (e) {
      debugPrint('Error updating cities: $e');
    } finally {
      if (mounted) {
        setState(() {
          _loadingLocation = false;
        });
      }
    }
  }

  @override
  void dispose() {
    _volumeDebounceTimer?.cancel();
    super.dispose();
  }

  // Format Duration to hh:mm:ss
  String _formatDuration(Duration? duration) {
    if (duration == null) return '--:--:--';
    final hours = duration.inHours.toString().padLeft(2, '0');
    final minutes = (duration.inMinutes % 60).toString().padLeft(2, '0');
    final seconds = (duration.inSeconds % 60).toString().padLeft(2, '0');
    return '$hours:$minutes:$seconds';
  }

  // Get glow color based on playback state
  Color _getStateColor(DeviceStatusModel status) {
    if (!status.isPlaying) return Colors.white30;
    if (status.isAdhan) return const Color(0xFFF1C40F); // Gold for Adhan
    if (status.statusText.contains('تشغيل')) return const Color(0xFF00F2FE); // Cyan/Sky Blue for normal play
    return const Color(0xFF8E44AD); // Vibrant Pink for others/paused
  }

  // Get state string in Arabic
  String _getStateText(DeviceStatusModel status) {
    if (status.isAdhan) return 'رفع الأذان الآن';
    if (status.isPlaying) return 'قيد التشغيل';
    return 'متوقف';
  }

  // Confirm reboot dialog
  Future<void> _showRebootConfirmDialog(BuildContext context) async {
    return showDialog(
      context: context,
      builder: (BuildContext dialogContext) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
            title: Text(
              'إعادة تشغيل الجهاز',
              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
            ),
            content: Text(
              'هل أنت متأكد من رغبتك في إعادة تشغيل جهاز Vivo Smart؟',
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.of(dialogContext).pop(),
                child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
              ),
              ElevatedButton(
                onPressed: () async {
                  Navigator.of(dialogContext).pop();
                  final api = ref.read(apiServiceProvider);
                  final success = await api.rebootSystem();
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(success ? 'تم إرسال أمر إعادة التشغيل بنجاح' : 'فشل إرسال الأمر'),
                        backgroundColor: success ? Colors.green : Colors.redAccent,
                      ),
                    );
                  }
                },
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.redAccent,
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                ),
                child: Text('إعادة التشغيل', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
              ),
            ],
          ),
        );
      },
    );
  }

  // Confirm factory reset dialog
  Future<void> _showFactoryResetConfirmDialog(BuildContext context) async {
    return showDialog(
      context: context,
      builder: (BuildContext dialogContext) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
            title: Row(
              children: [
                const Icon(Icons.warning_amber_rounded, color: Colors.orangeAccent),
                const SizedBox(width: 8),
                Text(
                  'إعادة ضبط المصنع',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
                ),
              ],
            ),
            content: Text(
              'هل أنت متأكد من رغبتك في إعادة ضبط المصنع؟ سيتم حذف جميع الإعدادات المحفوظة وشبكات Wi-Fi والملفات.',
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.of(dialogContext).pop(),
                child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
              ),
              ElevatedButton(
                onPressed: () async {
                  Navigator.of(dialogContext).pop();
                  final api = ref.read(apiServiceProvider);
                  final success = await api.factoryReset();
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(success ? 'تم إرسال أمر إعادة ضبط المصنع بنجاح' : 'فشل إرسال الأمر'),
                        backgroundColor: success ? Colors.green : Colors.redAccent,
                      ),
                    );
                  }
                },
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.orangeAccent,
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                ),
                child: Text('مسح البيانات والضبط', style: GoogleFonts.cairo(color: Colors.black87, fontWeight: FontWeight.bold)),
              ),
            ],
          ),
        );
      },
    );
  }

  // Confirm shutdown dialog
  Future<void> _showShutdownConfirmDialog(BuildContext context) async {
    return showDialog(
      context: context,
      builder: (BuildContext dialogContext) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
            title: Row(
              children: [
                const Icon(Icons.power_settings_new, color: Colors.redAccent),
                const SizedBox(width: 8),
                Text(
                  'إيقاف التشغيل النهائي',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
                ),
              ],
            ),
            content: Text(
              'هل أنت متأكد من رغبتك في إيقاف تشغيل جهاز Vivo Smart نهائياً؟ ستحتاج إلى إعادة توصيل الطاقة لتشغيله مجدداً.',
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.of(dialogContext).pop(),
                child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
              ),
              ElevatedButton(
                onPressed: () async {
                  Navigator.of(dialogContext).pop();
                  final api = ref.read(apiServiceProvider);
                  final success = await api.shutdownSystem();
                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(success ? 'تم إرسال أمر إيقاف التشغيل بنجاح' : 'فشل إرسال الأمر'),
                        backgroundColor: success ? Colors.green : Colors.redAccent,
                      ),
                    );
                  }
                },
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.redAccent,
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                ),
                child: Text('إيقاف التشغيل', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
              ),
            ],
          ),
        );
      },
    );
  }


  void _showAddOrEditDeviceDialog(BuildContext context, {SavedEspDevice? existingDevice}) {
    final isEditing = existingDevice != null;
    final nameController = TextEditingController(text: existingDevice?.name ?? '');
    final ipController = TextEditingController(text: existingDevice?.ip ?? '');
    final passController = TextEditingController(text: existingDevice?.password ?? '');

    showDialog(
      context: context,
      builder: (dialogCtx) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
          title: Text(
            isEditing ? 'تعديل بيانات الجهاز' : 'إضافة جهاز ESP جديد',
            style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
          ),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                TextField(
                  controller: nameController,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    labelText: 'اسم الجهاز المخصص',
                    labelStyle: GoogleFonts.cairo(color: Colors.white70),
                    hintText: 'مثال: المسجد الكبير أو جهاز الغرفة',
                    hintStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.05),
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: ipController,
                  enabled: !isEditing, // IP is unique identifier
                  style: const TextStyle(color: Colors.white),
                  textDirection: TextDirection.ltr,
                  decoration: InputDecoration(
                    labelText: 'عنوان IP لجهاز ESP',
                    labelStyle: GoogleFonts.cairo(color: Colors.white70),
                    hintText: '192.168.1.100',
                    hintStyle: const TextStyle(color: Colors.white30, fontSize: 12),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.05),
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: passController,
                  obscureText: true,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    labelText: 'كلمة المرور',
                    labelStyle: GoogleFonts.cairo(color: Colors.white70),
                    hintText: 'كلمة مرور الجهاز (إن وجدت)',
                    hintStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.05),
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(dialogCtx).pop(),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              style: ElevatedButton.styleFrom(
                backgroundColor: const Color(0xFF00F2FE),
                foregroundColor: Colors.black,
              ),
              onPressed: () async {
                final ip = ipController.text.trim();
                final name = nameController.text.trim();
                final pwd = passController.text;
                if (ip.isEmpty) return;

                await ref.read(savedDevicesProvider.notifier).saveOrUpdateDevice(ip, name, pwd);
                // Also update nickname provider for backward compatibility
                if (name.isNotEmpty) {
                  await ref.read(deviceNicknamesProvider.notifier).setNickname(ip, name);
                }
                if (dialogCtx.mounted) {
                  Navigator.of(dialogCtx).pop();
                }
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(isEditing ? 'تم تحديث بيانات الجهاز' : 'تم إضافة الجهاز بنجاح'),
                      backgroundColor: Colors.green,
                    ),
                  );
                }
              },
              child: Text(isEditing ? 'حفظ التعديلات' : 'إضافة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            ),
          ],
        ),
      ),
    );
  }

  void _showSwitchDevicesDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (dialogCtx) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: Dialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(24),
              side: BorderSide(color: Colors.white.withOpacity(0.1)),
            ),
            insetPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
            child: Container(
              width: 540,
              constraints: BoxConstraints(
                maxHeight: MediaQuery.of(context).size.height * 0.8,
              ),
              child: Consumer(
                builder: (ctx, refConsumer, _) {
                  final savedDevices = refConsumer.watch(savedDevicesProvider);
                  final currentConn = refConsumer.watch(connectionStateProvider);
                  final currentIp = currentConn.ip ?? '';

                  return Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Padding(
                        padding: const EdgeInsets.fromLTRB(16, 20, 20, 12),
                        child: Row(
                          children: [
                            Container(
                              padding: const EdgeInsets.all(8),
                              decoration: BoxDecoration(
                                color: const Color(0xFF00F2FE).withOpacity(0.12),
                                borderRadius: BorderRadius.circular(10),
                              ),
                              child: const Icon(Icons.devices, color: Color(0xFF00F2FE), size: 24),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    'الأجهزة المحفوظة والتبديل',
                                    style: GoogleFonts.cairo(
                                      color: Colors.white,
                                      fontSize: 18,
                                      fontWeight: FontWeight.bold,
                                    ),
                                  ),
                                  Text(
                                    'اختر جهازاً للاتصال السريع أو أضف جهازاً جديداً',
                                    style: GoogleFonts.cairo(
                                      color: Colors.white54,
                                      fontSize: 12,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            IconButton(
                              icon: const Icon(Icons.add_circle, color: Color(0xFF00F2FE), size: 28),
                              tooltip: 'إضافة جهاز جديد',
                              onPressed: () => _showAddOrEditDeviceDialog(context),
                            ),
                            IconButton(
                              icon: const Icon(Icons.close, color: Colors.white54, size: 22),
                              tooltip: 'إغلاق',
                              onPressed: () => Navigator.of(dialogCtx).pop(),
                            ),
                          ],
                        ),
                      ),
                      const Divider(color: Colors.white10, height: 1),
                      if (savedDevices.isEmpty)
                        Padding(
                          padding: const EdgeInsets.all(32.0),
                          child: Column(
                            children: [
                              const Icon(Icons.router_outlined, color: Colors.white30, size: 48),
                              const SizedBox(height: 12),
                              Text(
                                'لا توجد أجهزة ESP محفوظة حالياً',
                                style: GoogleFonts.cairo(color: Colors.white54, fontSize: 14),
                              ),
                              const SizedBox(height: 16),
                              ElevatedButton.icon(
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: const Color(0xFF00F2FE),
                                  foregroundColor: Colors.black,
                                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                                onPressed: () => _showAddOrEditDeviceDialog(context),
                                icon: const Icon(Icons.add),
                                label: Text('إضافة جهاز جديد', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                              ),
                            ],
                          ),
                        )
                      else
                        Flexible(
                          child: ListView.separated(
                            shrinkWrap: true,
                            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                            itemCount: savedDevices.length,
                            separatorBuilder: (_, __) => const SizedBox(height: 10),
                            itemBuilder: (ctx, index) {
                              final dev = savedDevices[index];
                              final isActive = dev.ip == currentIp;

                              return Material(
                                color: Colors.transparent,
                                child: InkWell(
                                  onTap: isActive
                                      ? null
                                      : () async {
                                          Navigator.of(dialogCtx).pop();
                                          ScaffoldMessenger.of(context).showSnackBar(
                                            SnackBar(
                                              content: Text('جاري التبديل إلى: ${dev.name.isNotEmpty ? dev.name : dev.ip}...'),
                                              backgroundColor: const Color(0xFF1C132E),
                                              duration: const Duration(seconds: 2),
                                            ),
                                          );
                                          final success = await refConsumer
                                              .read(connectionStateProvider.notifier)
                                              .switchToDevice(dev.ip, dev.password);
                                          if (context.mounted) {
                                            if (success) {
                                              ScaffoldMessenger.of(context).showSnackBar(
                                                SnackBar(
                                                  content: Text('تم الاتصال بنجاح بـ ${dev.name.isNotEmpty ? dev.name : dev.ip}'),
                                                  backgroundColor: Colors.green,
                                                ),
                                              );
                                            } else {
                                              ScaffoldMessenger.of(context).showSnackBar(
                                                SnackBar(
                                                  content: Text('فشل الاتصال بـ ${dev.ip}. تحقق من الشبكة وكلمة المرور.'),
                                                  backgroundColor: Colors.redAccent,
                                                ),
                                              );
                                            }
                                          }
                                        },
                                  borderRadius: BorderRadius.circular(16),
                                  child: Container(
                                    padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
                                    decoration: BoxDecoration(
                                      color: isActive
                                          ? const Color(0x3300F2FE)
                                          : const Color(0x7316132C),
                                      borderRadius: BorderRadius.circular(16),
                                      border: Border.all(
                                        color: isActive
                                            ? const Color(0xFF00F2FE)
                                            : Colors.white.withOpacity(0.08),
                                        width: isActive ? 1.5 : 1.0,
                                      ),
                                    ),
                                    child: Row(
                                      children: [
                                        Container(
                                          width: 40,
                                          height: 40,
                                          decoration: BoxDecoration(
                                            color: isActive
                                                ? const Color(0xFF00F2FE).withOpacity(0.2)
                                                : Colors.white.withOpacity(0.05),
                                            shape: BoxShape.circle,
                                          ),
                                          child: Icon(
                                            isActive ? Icons.check_circle : Icons.router,
                                            color: isActive ? const Color(0xFF00F2FE) : Colors.white60,
                                            size: 22,
                                          ),
                                        ),
                                        const SizedBox(width: 12),
                                        Expanded(
                                          child: Column(
                                            crossAxisAlignment: CrossAxisAlignment.start,
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                              Row(
                                                children: [
                                                  Flexible(
                                                    child: Text(
                                                      dev.name.isNotEmpty ? dev.name : dev.ip,
                                                      style: GoogleFonts.cairo(
                                                        color: Colors.white,
                                                        fontWeight: FontWeight.bold,
                                                        fontSize: 15,
                                                      ),
                                                      maxLines: 1,
                                                      overflow: TextOverflow.ellipsis,
                                                    ),
                                                  ),
                                                  if (isActive) ...[
                                                    const SizedBox(width: 8),
                                                    Container(
                                                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                                                      decoration: BoxDecoration(
                                                        color: const Color(0xFF00F2FE),
                                                        borderRadius: BorderRadius.circular(6),
                                                      ),
                                                      child: Text(
                                                        'متصل حالياً',
                                                        style: GoogleFonts.cairo(
                                                          color: Colors.black,
                                                          fontWeight: FontWeight.bold,
                                                          fontSize: 11,
                                                        ),
                                                      ),
                                                    ),
                                                  ],
                                                ],
                                              ),
                                              const SizedBox(height: 3),
                                              Text(
                                                dev.ip,
                                                style: GoogleFonts.cairo(
                                                  color: Colors.white60,
                                                  fontSize: 13,
                                                ),
                                                textDirection: TextDirection.ltr,
                                              ),
                                            ],
                                          ),
                                        ),
                                        const SizedBox(width: 8),
                                        Row(
                                          mainAxisSize: MainAxisSize.min,
                                          children: [
                                            IconButton(
                                              icon: const Icon(Icons.edit_outlined, color: Colors.white54, size: 20),
                                              tooltip: 'تعديل',
                                              visualDensity: VisualDensity.compact,
                                              constraints: const BoxConstraints(minWidth: 36, minHeight: 36),
                                              onPressed: () => _showAddOrEditDeviceDialog(context, existingDevice: dev),
                                            ),
                                            const SizedBox(width: 4),
                                            IconButton(
                                              icon: const Icon(Icons.delete_outline, color: Colors.redAccent, size: 20),
                                              tooltip: 'حذف',
                                              visualDensity: VisualDensity.compact,
                                              constraints: const BoxConstraints(minWidth: 36, minHeight: 36),
                                              onPressed: () async {
                                                await refConsumer.read(savedDevicesProvider.notifier).removeDevice(dev.ip);
                                              },
                                            ),
                                          ],
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              );
                            },
                          ),
                        ),
                      const SizedBox(height: 12),
                    ],
                  );
                },
              ),
            ),
          ),
        );
      },
    );
  }

  void _showRenameDeviceDialog(BuildContext context, String ip) {
    final controller = TextEditingController(
      text: ref.read(deviceNicknamesProvider)[ip] ?? '',
    );
    showDialog(
      context: context,
      builder: (dialogContext) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
            title: Text(
              'تسمية الجهاز',
              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
            ),
            content: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'أدخل اسماً مخصصاً للتعرف على هذا الجهاز بسهولة:',
                  style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13),
                ),
                const SizedBox(height: 12),
                TextField(
                  controller: controller,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    hintText: 'مثال: المسجد الكبير، جهاز الصالة',
                    hintStyle: GoogleFonts.cairo(color: Colors.white30),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.05),
                    enabledBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                    ),
                    focusedBorder: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                    ),
                  ),
                ),
              ],
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.of(dialogContext).pop(),
                child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
              ),
              ElevatedButton(
                onPressed: () async {
                  final newName = controller.text.trim();
                  await ref.read(deviceNicknamesProvider.notifier).setNickname(ip, newName);
                  await ref.read(savedDevicesProvider.notifier).saveOrUpdateDevice(ip, newName, '');
                  if (mounted) {
                    Navigator.of(dialogContext).pop();
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text('تم حفظ الاسم المخصص بنجاح'),
                        backgroundColor: Colors.green,
                      ),
                    );
                  }
                },
                child: Text('حفظ', style: GoogleFonts.cairo(color: Colors.black)),
              ),
            ],
          ),
        );
      },
    );
  }

  void _showSetupApDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (dialogCtx) {
        return Directionality(
          textDirection: TextDirection.rtl,
          child: Dialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(24),
              side: BorderSide(color: Colors.white.withOpacity(0.1)),
            ),
            insetPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
            child: Container(
              width: 520,
              constraints: BoxConstraints(
                maxHeight: MediaQuery.of(context).size.height * 0.85,
              ),
              child: SingleChildScrollView(
                padding: const EdgeInsets.all(16),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.cell_tower, color: Color(0xFF00F2FE), size: 24),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            'نقطة اتصال الطوارئ (Setup AP)',
                            style: GoogleFonts.cairo(
                              color: Colors.white,
                              fontSize: 18,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                        ),
                        IconButton(
                          icon: const Icon(Icons.close, color: Colors.white54, size: 22),
                          onPressed: () => Navigator.of(dialogCtx).pop(),
                        ),
                      ],
                    ),
                    const Divider(color: Colors.white10),
                    const SizedBox(height: 10),
                    const _ApSettingsCard(),
                  ],
                ),
              ),
            ),
          ),
        );
      },
    );
  }

  Future<void> _handleRefreshAll(BuildContext context) async {
    try {
      await ref.read(statusPollingProvider.notifier).pollStatus();
      ref.invalidate(filesProvider);
      ref.invalidate(alertsProvider);
      ref.invalidate(adhanConfigProvider);
      ref.invalidate(eidStateProvider);
      ref.invalidate(gpioProvider);
      ref.invalidate(wifiProvider);
      ref.invalidate(logsProvider);
      ref.invalidate(presenceProvider);
      ref.invalidate(maghribAlertsProvider);

      if (context.mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Row(
              children: [
                const Icon(Icons.check_circle_outline, color: Colors.greenAccent, size: 20),
                const SizedBox(width: 10),
                Text('تم تحديث الاتصال والبيانات بنجاح', style: GoogleFonts.cairo(color: Colors.white)),
              ],
            ),
            backgroundColor: const Color(0xFF1C132E),
            duration: const Duration(seconds: 2),
            behavior: SnackBarBehavior.floating,
          ),
        );
      }
    } catch (e) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('تعذر تحديث الاتصال: $e', style: GoogleFonts.cairo(color: Colors.white)),
            backgroundColor: Colors.redAccent,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final status = ref.watch(statusPollingProvider);
    final connection = ref.watch(connectionStateProvider);

    // Sync volume slider with server if user is not sliding
    if (_localVolume == null) {
      _localVolume = status.volume.toDouble();
    }

    return Directionality(
      textDirection: TextDirection.rtl,
      child: Container(
        decoration: const BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [
              Color(0xFF3498DB), // Primary Blue
              Color(0xFF8E44AD), // Primary Purple
              Color(0xFF2C1340), // Deep Purple
            ],
          ),
        ),
        child: Scaffold(
          backgroundColor: Colors.transparent,
          appBar: PreferredSize(
            preferredSize: const Size.fromHeight(kToolbarHeight),
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 880),
                child: AppBar(
                  backgroundColor: Colors.transparent,
                  elevation: 0,
                  titleSpacing: 16,
                  title: Row(
                    children: [
                      Container(
                        width: 32,
                        height: 32,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: status.isWifiConnected
                              ? const Color(0x262ECC71) // Green with 15% opacity (rgba(46,204,113,0.15))
                              : const Color(0x26E74C3C), // Red with 15% opacity (rgba(231,76,60,0.15))
                          border: Border.all(
                            color: status.isWifiConnected
                                ? const Color(0x4D2ECC71) // Green with 30% opacity
                                : const Color(0x4DE74C3C), // Red with 30% opacity
                            width: 1.2,
                          ),
                          boxShadow: [
                            BoxShadow(
                              color: (status.isWifiConnected
                                      ? const Color(0xFF2ECC71)
                                      : const Color(0xFFE74C3C))
                                  .withOpacity(0.2),
                              blurRadius: 8,
                              spreadRadius: 1,
                            ),
                          ],
                        ),
                        child: Center(
                          child: Icon(
                            status.isWifiConnected ? Icons.wifi : Icons.wifi_off,
                            color: status.isWifiConnected
                                ? const Color(0xFF2ECC71)
                                : const Color(0xFFE74C3C),
                            size: 16,
                          ),
                        ),
                      ),
                      const SizedBox(width: 12),
                      Text(
                        'VIVO SMART',
                        style: GoogleFonts.cairo(
                          fontWeight: FontWeight.w900,
                          fontSize: 18,
                          letterSpacing: 1,
                          color: Colors.white,
                        ),
                      ),
                    ],
                  ),
                  actions: [
                    GestureDetector(
                      onTap: () => _showSwitchDevicesDialog(context),
                      child: Padding(
                        padding: const EdgeInsets.symmetric(vertical: 12.0),
                        child: Container(
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                          decoration: BoxDecoration(
                            color: const Color(0x7316132C),
                            borderRadius: BorderRadius.circular(10),
                            border: Border.all(color: Colors.white.withOpacity(0.08)),
                          ),
                          child: Center(
                            child: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                const Icon(Icons.devices, color: Color(0xFFADCCFF), size: 14),
                                const SizedBox(width: 4),
                                Text(
                                  ref.watch(deviceNicknamesProvider)[connection.ip] ?? connection.ip ?? '',
                                  style: GoogleFonts.cairo(
                                    color: Colors.white,
                                    fontSize: 12,
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                                const SizedBox(width: 2),
                                const Icon(Icons.arrow_drop_down, color: Color(0xFFADCCFF), size: 16),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
                    IconButton(
                      icon: const Icon(Icons.sync_rounded, color: Color(0xFF00F2FE)),
                      tooltip: 'تحديث الاتصال والبيانات',
                      onPressed: () => _handleRefreshAll(context),
                    ),
                    PopupMenuButton<String>(
                      icon: const Icon(Icons.more_vert, color: Colors.white),
                      color: const Color(0xFF1C132E),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                      onSelected: (value) async {
                        if (value == 'switch_device') {
                          _showSwitchDevicesDialog(context);
                        } else if (value == 'setup_ap') {
                          _showSetupApDialog(context);
                        } else if (value == 'refresh') {
                          await _handleRefreshAll(context);
                        } else if (value == 'rename') {
                          _showRenameDeviceDialog(context, connection.ip ?? '');
                        } else if (value == 'logout') {
                          await ref.read(connectionStateProvider.notifier).logout();
                        }
                      },
                      itemBuilder: (context) => [
                        PopupMenuItem(
                          value: 'switch_device',
                          child: Row(
                            children: [
                              const Icon(Icons.devices, color: Color(0xFF00F2FE)),
                              const SizedBox(width: 10),
                              Text('تبديل / إدارة الأجهزة', style: GoogleFonts.cairo(color: Colors.white)),
                            ],
                          ),
                        ),
                        PopupMenuItem(
                          value: 'setup_ap',
                          child: Row(
                            children: [
                              const Icon(Icons.cell_tower, color: Color(0xFF00F2FE)),
                              const SizedBox(width: 10),
                              Text('نقطة اتصال الطوارئ (Setup AP)', style: GoogleFonts.cairo(color: Colors.white)),
                            ],
                          ),
                        ),
                        PopupMenuItem(
                          value: 'refresh',
                          child: Row(
                            children: [
                              const Icon(Icons.sync_rounded, color: Color(0xFF00F2FE)),
                              const SizedBox(width: 10),
                              Text('تحديث الاتصال', style: GoogleFonts.cairo(color: Colors.white)),
                            ],
                          ),
                        ),
                        PopupMenuItem(
                          value: 'rename',
                          child: Row(
                            children: [
                              const Icon(Icons.edit_note, color: Color(0xFF00F2FE)),
                              const SizedBox(width: 10),
                              Text('تسمية الجهاز', style: GoogleFonts.cairo(color: Colors.white)),
                            ],
                          ),
                        ),
                        PopupMenuItem(
                          value: 'logout',
                          child: Row(
                            children: [
                              const Icon(Icons.logout, color: const Color(0xFFADCCFF)),
                              const SizedBox(width: 10),
                              Text('تسجيل الخروج', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ),
          ),
          body: SafeArea(
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 880),
                child: _buildActiveTab(status),
              ),
            ),
          ),
          bottomNavigationBar: Container(
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              border: Border(
                top: BorderSide(color: Colors.white.withOpacity(0.08), width: 1.0),
              ),
            ),
            child: SafeArea(
              child: Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 880),
                    child: SizedBox(
                      width: MediaQuery.of(context).size.width.clamp(0.0, 880.0),
                      child: BottomNavigationBar(
                        currentIndex: _bottomNavIndex,
                        onTap: (index) {
                          setState(() {
                            _bottomNavIndex = index;
                          });
                        },
                        backgroundColor: Colors.transparent,
                        elevation: 0,
                        type: BottomNavigationBarType.fixed,
                        selectedItemColor: Colors.white,
                        unselectedItemColor: Colors.white38,
                        selectedLabelStyle: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 11),
                        unselectedLabelStyle: GoogleFonts.cairo(fontSize: 10),
                        items: const [
                          BottomNavigationBarItem(
                            icon: Icon(Icons.home_outlined),
                            activeIcon: Icon(Icons.home),
                            label: 'الرئيسية',
                          ),
                          BottomNavigationBarItem(
                            icon: Icon(Icons.calendar_month_outlined),
                            activeIcon: Icon(Icons.calendar_month),
                            label: 'التقاويم',
                          ),
                          BottomNavigationBarItem(
                            icon: Icon(Icons.tune_outlined),
                            activeIcon: Icon(Icons.tune),
                            label: 'التحكم',
                          ),
                          BottomNavigationBarItem(
                            icon: Icon(Icons.folder_open_outlined),
                            activeIcon: Icon(Icons.folder_open),
                            label: 'الملفات',
                          ),
                          BottomNavigationBarItem(
                            icon: Icon(Icons.settings_outlined),
                            activeIcon: Icon(Icons.settings),
                            label: 'الضبط',
                          ),
                        ],
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildActiveTab(DeviceStatusModel status) {
    switch (_bottomNavIndex) {
      case 0:
        return _buildHomeTab(status);
      case 1:
        return Column(
          children: [
            _buildSubTabSwitcher(
              currentIndex: _prayersSubTabIndex,
              titles: const ['ضبط المواقيت', 'إدارة الروزمانة والتقاويم (CSV)'],
              onTap: (index) {
                setState(() {
                  _prayersSubTabIndex = index;
                });
              },
            ),
            Expanded(
              child: _prayersSubTabIndex == 0
                  ? _buildPrayerTab(status)
                  : const SingleChildScrollView(
                      padding: EdgeInsets.all(16.0),
                      child: _CsvCalendarManagerCard(),
                    ),
            ),
          ],
        );
      case 2:
        return Column(
          children: [
            _buildSubTabSwitcher(
              currentIndex: _controlSubTabIndex,
              titles: const ['التنبيهات', 'تنبيهات المغرب', 'مخططات التحكم', 'الأذان والإقامة', 'وضع العيد', 'كشف الحضور'],
              onTap: (index) {
                setState(() {
                  _controlSubTabIndex = index;
                });
              },
            ),
            Expanded(
              child: _controlSubTabIndex == 0
                  ? _buildAlertsTab()
                  : _controlSubTabIndex == 1
                      ? _buildMaghribTab()
                      : _controlSubTabIndex == 2
                          ? _buildGpioTab()
                          : _controlSubTabIndex == 3
                              ? const SingleChildScrollView(
                                  padding: EdgeInsets.all(16.0),
                                  child: _AdhanSettingsCard(),
                                )
                              : _controlSubTabIndex == 4
                                  ? _buildEidTab()
                                  : const SingleChildScrollView(
                                      padding: EdgeInsets.all(16.0),
                                      child: _PresenceSettingsCard(),
                                    ),
            ),
          ],
        );
      case 3:
        return Column(
          children: [
            _buildSubTabSwitcher(
              currentIndex: _filesSubTabIndex,
              titles: const ['ملفات الذاكرة (SD)', 'راديو القرآن الكريم'],
              onTap: (index) {
                setState(() {
                  _filesSubTabIndex = index;
                });
              },
            ),
            Expanded(
              child: _filesSubTabIndex == 0
                  ? _buildFilesTab()
                  : _QuranRadioTab(
                      onSchedule: (url, name) {
                        setState(() {
                          _bottomNavIndex = 2; // Control tab
                          _controlSubTabIndex = 0; // Alerts subtab
                        });
                        WidgetsBinding.instance.addPostFrameCallback((_) {
                          _showAddEditAlertSheet(context, null, 0, initialRadioUrl: url, initialRadioName: name);
                        });
                      },
                    ),
            ),
          ],
        );
      case 4:
        return Column(
          children: [
            _buildSubTabSwitcher(
              currentIndex: _settingsSubTabIndex,
              titles: const ['ضبط النظام والشبكة', 'سجل الأحداث'],
              onTap: (index) {
                setState(() {
                  _settingsSubTabIndex = index;
                });
              },
            ),
            Expanded(
              child: _settingsSubTabIndex == 0
                  ? _buildSettingsTab()
                  : _buildLogsTab(),
            ),
          ],
        );
      default:
        return _buildHomeTab(status);
    }
  }

  Widget _buildSubTabSwitcher({
    required int currentIndex,
    required List<String> titles,
    required ValueChanged<int> onTap,
  }) {
    final isScrollable = titles.length > 3;

    final items = List.generate(titles.length, (index) {
      final title = titles[index];
      final isSelected = currentIndex == index;
      final childWidget = GestureDetector(
        onTap: () => onTap(index),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 200),
          padding: EdgeInsets.symmetric(
            vertical: 8,
            horizontal: isScrollable ? 14 : 4,
          ),
          margin: isScrollable ? const EdgeInsets.symmetric(horizontal: 3) : EdgeInsets.zero,
          decoration: BoxDecoration(
            color: isSelected ? Colors.white.withOpacity(0.12) : Colors.transparent,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(
              color: isSelected ? Colors.white.withOpacity(0.1) : Colors.transparent,
            ),
          ),
          child: Center(
            child: FittedBox(
              fit: BoxFit.scaleDown,
              child: Text(
                title,
                maxLines: 1,
                style: GoogleFonts.cairo(
                  color: isSelected ? Colors.white : Colors.white60,
                  fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
                  fontSize: 12,
                ),
              ),
            ),
          ),
        ),
      );

      if (isScrollable) {
        return childWidget;
      }
      return Expanded(child: childWidget);
    });

    return Container(
      padding: const EdgeInsets.all(4),
      margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
      ),
      child: isScrollable
          ? SingleChildScrollView(
              scrollDirection: Axis.horizontal,
              physics: const BouncingScrollPhysics(),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: items,
              ),
            )
          : Row(
              children: items,
            ),
    );
  }

  Widget _buildHomeTab(DeviceStatusModel status) {
    final stateColor = _getStateColor(status);
    final filesState = ref.watch(filesProvider);
    final quickPlayerState = ref.watch(quickPlayerProvider);

    final List<String> audioFiles = [];
    filesState.whenData((data) {
      final List filesList = data['files'] as List? ?? [];
      for (var f in filesList) {
        final String name = f['name'] ?? '';
        final bool isDir = f['isDirectory'] ?? false;
        if (!isDir && !name.startsWith('prayer_csv/') && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
          audioFiles.add(name);
        }
      }
    });

    if (audioFiles.isNotEmpty) {
      final matched = _findMatchingFile(audioFiles, _selectedQuickPlayerFile);
      _selectedQuickPlayerFile = matched ?? audioFiles.first;
    } else {
      _selectedQuickPlayerFile = null;
    }

    final isAdhan = quickPlayerState.isAdhan;
    final hasActiveMedia = quickPlayerState.duration > 0 || status.isPlaying;

    return RefreshIndicator(
      color: const Color(0xFF00F2FE),
      backgroundColor: const Color(0xFF1C132E),
      onRefresh: () => _handleRefreshAll(context),
      child: SingleChildScrollView(
        physics: const AlwaysScrollableScrollPhysics(),
        padding: const EdgeInsets.all(20.0),
        child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          IntrinsicHeight(
            child: Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Expanded(
                  flex: 3,
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        FittedBox(
                          fit: BoxFit.scaleDown,
                          child: Text(
                            _formatTime(status.currentClockTime, ref.watch(timeFormatProvider)),
                            style: GoogleFonts.orbitron(
                              fontSize: 32,
                              fontWeight: FontWeight.bold,
                              color: Colors.white,
                              letterSpacing: 1,
                            ),
                          ),
                        ),
                        const SizedBox(height: 4),
                        FittedBox(
                          fit: BoxFit.scaleDown,
                          child: Text(
                            'ساعة النظام الحالية',
                            style: GoogleFonts.cairo(color: Colors.white38, fontSize: 10),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(width: 12),
                Expanded(
                  flex: 4,
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.calendar_month, color: Color(0xFF00F2FE), size: 16),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                status.gregorianDate.isNotEmpty
                                    ? '${_getDayName(status.gregorianDate)} - ${status.gregorianDate}'
                                    : 'جاري التحميل...',
                                style: GoogleFonts.cairo(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold),
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                          ],
                        ),
                        const Divider(color: Colors.white10, height: 12),
                        Row(
                          children: [
                            const Icon(Icons.star_border, color: Color(0xFF8E44AD), size: 16),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                status.hijriDate.isNotEmpty ? status.hijriDate : 'جاري التحميل...',
                                style: GoogleFonts.cairo(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold),
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(color: Colors.white.withOpacity(0.08)),
              boxShadow: [
                BoxShadow(
                  color: const Color(0xFF8E44AD).withOpacity(0.03),
                  blurRadius: 15,
                  spreadRadius: 2,
                ),
              ],
            ),
            child: Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        'الصلاة القادمة',
                        style: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      ),
                      const SizedBox(height: 4),
                      Wrap(
                        spacing: 10,
                        runSpacing: 4,
                        crossAxisAlignment: WrapCrossAlignment.center,
                        children: [
                          Text(
                            status.nextPrayerName ?? '--',
                            style: GoogleFonts.cairo(
                              color: const Color(0xFFF1C40F), // Vibrant Gold
                              fontSize: 26,
                              fontWeight: FontWeight.w900,
                              height: 1.2,
                              shadows: [
                                BoxShadow(
                                  color: const Color(0xFFF1C40F).withOpacity(0.4),
                                  blurRadius: 10,
                                ),
                              ],
                            ),
                          ),
                          Text(
                            status.nextPrayerTime != null ? 'في ${_formatTime(status.nextPrayerTime!, ref.watch(timeFormatProvider))}' : '',
                            style: GoogleFonts.cairo(
                              color: const Color(0xFFADCCFF),
                              fontSize: 14,
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                  decoration: BoxDecoration(
                    color: const Color(0x7316132C),
                    borderRadius: BorderRadius.circular(16),
                    border: Border.all(
                      color: const Color(0xFF00F2FE).withOpacity(0.3), // Neon Cyan Border
                    ),
                  ),
                  child: Column(
                    children: [
                      Text(
                        _formatDuration(status.timeToNextPrayer),
                        style: GoogleFonts.orbitron(
                          color: const Color(0xFF00F2FE), // Neon Cyan Countdown
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                          letterSpacing: 1,
                          shadows: [
                            BoxShadow(
                              color: const Color(0xFF00F2FE).withOpacity(0.4),
                              blurRadius: 10,
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        'متبقي للأذان',
                        style: GoogleFonts.cairo(color: Colors.white38, fontSize: 9),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),

          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(
                color: stateColor.withOpacity(0.3),
                width: 1.2,
              ),
              boxShadow: [
                BoxShadow(
                  color: stateColor.withOpacity(0.03),
                  blurRadius: 15,
                  offset: const Offset(0, 8),
                ),
              ],
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                      decoration: BoxDecoration(
                        color: stateColor.withOpacity(0.15),
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: Text(
                        _getStateText(status),
                        style: GoogleFonts.cairo(
                          color: stateColor,
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                    const Spacer(),
                    if (isAdhan) ...[
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                        decoration: BoxDecoration(
                          color: Colors.amber.withOpacity(0.2),
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: Colors.amber.withOpacity(0.4)),
                        ),
                        child: Text(
                          'أذان / إقامة نشط',
                          style: GoogleFonts.cairo(color: Colors.amber, fontSize: 10, fontWeight: FontWeight.bold),
                        ),
                      ),
                    ] else if (status.isPlaying) ...[
                      const Icon(Icons.music_note, color: Color(0xFF00FFCC), size: 18),
                    ],
                  ],
                ),
                const SizedBox(height: 12),
                Text(
                  status.statusText,
                  style: GoogleFonts.cairo(
                    fontSize: 15,
                    fontWeight: FontWeight.bold,
                    color: Colors.white,
                  ),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                ),
                if (status.isPlaying && status.currentFile.isNotEmpty) ...[
                  const SizedBox(height: 6),
                  Text(
                    status.currentFile,
                    style: const TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 11,
                      color: Colors.white38,
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    textDirection: TextDirection.ltr,
                  ),
                ],
                const Divider(color: Colors.white10, height: 24),
                Text(
                  'المشغل السريع والتحكم الفوري',
                  style: GoogleFonts.cairo(
                    fontSize: 12,
                    fontWeight: FontWeight.bold,
                    color: const Color(0xFFADCCFF),
                  ),
                ),
                const SizedBox(height: 8),
                if (audioFiles.isEmpty)
                  Padding(
                    padding: const EdgeInsets.symmetric(vertical: 8.0),
                    child: Text(
                      'لا توجد ملفات صوتية متوفرة على كرت الذاكرة لتشغيلها',
                      style: GoogleFonts.cairo(color: Colors.white30, fontSize: 11),
                      textAlign: TextAlign.center,
                    ),
                  )
                else ...[
                  DropdownButtonFormField<String>(
                    isExpanded: true,
                    dropdownColor: const Color(0xFF1C132E),
                    style: const TextStyle(color: Colors.white, fontSize: 13),
                    value: _findMatchingFile(audioFiles, _selectedQuickPlayerFile),
                    decoration: InputDecoration(
                      filled: true,
                      fillColor: Colors.black26,
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                    ),
                    items: audioFiles.map((file) {
                      return DropdownMenuItem<String>(
                        value: file,
                        child: Text(file, textDirection: TextDirection.ltr, overflow: TextOverflow.ellipsis),
                      );
                    }).toList(),
                    onChanged: (val) {
                      setState(() {
                        _selectedQuickPlayerFile = val;
                      });
                    },
                  ),
                  const SizedBox(height: 12),
                  if (hasActiveMedia) ...[
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Text(
                          _formatSeconds(quickPlayerState.position),
                          style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 11),
                        ),
                        Text(
                          _formatSeconds(quickPlayerState.duration),
                          style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 11),
                        ),
                      ],
                    ),
                    SliderTheme(
                      data: SliderTheme.of(context).copyWith(
                        activeTrackColor: const Color(0xFF8E44AD),
                        inactiveTrackColor: Colors.white.withOpacity(0.1),
                        thumbColor: const Color(0xFF8E44AD),
                        trackHeight: 3,
                        thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 5),
                        overlayShape: const RoundSliderOverlayShape(overlayRadius: 10),
                      ),
                      child: Slider(
                        value: quickPlayerState.position.toDouble().clamp(0.0, quickPlayerState.duration.toDouble()),
                        min: 0.0,
                        max: quickPlayerState.duration > 0 ? quickPlayerState.duration.toDouble() : 100.0,
                        onChanged: (val) {},
                        onChangeEnd: (val) {
                          ref.read(quickPlayerProvider.notifier).seek(val.toInt());
                        },
                      ),
                    ),
                    const SizedBox(height: 4),
                  ],
                  FittedBox(
                    fit: BoxFit.scaleDown,
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        IconButton(
                          icon: const Icon(Icons.stop_circle_outlined, color: Colors.redAccent, size: 26),
                        tooltip: 'إيقاف',
                        onPressed: isAdhan ? null : () async {
                          final success = await ref.read(quickPlayerProvider.notifier).stop();
                          if (!success) {
                            await ref.read(apiServiceProvider).stopAudio();
                          }
                          ref.read(statusPollingProvider.notifier).pollStatus();
                        },
                      ),
                      const SizedBox(width: 6),
                      IconButton(
                        icon: const Icon(Icons.replay_10_rounded, color: const Color(0xFFADCCFF), size: 24),
                        tooltip: 'رجوع 10 ثواني',
                        onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).seekBackward(),
                      ),
                      const SizedBox(width: 8),
                      GestureDetector(
                        onTap: isAdhan ? null : () async {
                          final file = _selectedQuickPlayerFile;
                          if (quickPlayerState.playbackState == 1) {
                            await ref.read(quickPlayerProvider.notifier).togglePlayPause();
                          } else if (quickPlayerState.playbackState == 2) {
                            await ref.read(quickPlayerProvider.notifier).togglePlayPause();
                          } else {
                            if (file != null) {
                              await ref.read(quickPlayerProvider.notifier).play(file);
                            }
                          }
                        },
                        child: Container(
                          padding: const EdgeInsets.all(8),
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: isAdhan ? Colors.white10 : const Color(0xFF8E44AD),
                          ),
                          child: Icon(
                            (quickPlayerState.playbackState == 1) ? Icons.pause_rounded : Icons.play_arrow_rounded,
                            color: isAdhan ? Colors.white30 : Colors.white,
                            size: 26,
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      IconButton(
                        icon: const Icon(Icons.forward_10_rounded, color: const Color(0xFFADCCFF), size: 24),
                        tooltip: 'تقديم 10 ثواني',
                        onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).seekForward(),
                      ),
                      const SizedBox(width: 6),
                      IconButton(
                        icon: const Icon(Icons.playlist_add_rounded, color: Color(0xFF00F2FE), size: 26),
                        tooltip: 'إضافة للقائمة',
                        onPressed: () {
                          final file = _selectedQuickPlayerFile;
                          if (file != null) {
                            ref.read(quickPlayerProvider.notifier).addToPlaylist(file);
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text('تمت إضافة $file إلى القائمة المؤقتة', style: GoogleFonts.cairo(fontSize: 12)),
                                backgroundColor: const Color(0xFF1C132E),
                                duration: const Duration(seconds: 1),
                              ),
                            );
                          }
                        },
                      ),
                    ],
                  ),
                ),
                  if (quickPlayerState.playlist.isNotEmpty) ...[
                    const Divider(color: Colors.white10, height: 16),
                    Text(
                      'قائمة التشغيل المؤقتة (${quickPlayerState.playlist.length} ملفات)',
                      style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 6),
                    ConstrainedBox(
                      constraints: const BoxConstraints(maxHeight: 120),
                      child: ListView.separated(
                        shrinkWrap: true,
                        itemCount: quickPlayerState.playlist.length,
                        separatorBuilder: (context, index) => const Divider(color: Colors.white10, height: 1),
                        itemBuilder: (context, index) {
                          final file = quickPlayerState.playlist[index];
                          return ListTile(
                            contentPadding: EdgeInsets.zero,
                            dense: true,
                            title: Text(file, style: const TextStyle(color: const Color(0xFFADCCFF), fontSize: 12), textDirection: TextDirection.ltr, overflow: TextOverflow.ellipsis),
                            trailing: IconButton(
                              icon: const Icon(Icons.remove_circle_outline, color: Colors.redAccent, size: 16),
                              onPressed: () => ref.read(quickPlayerProvider.notifier).removeFromPlaylist(index),
                            ),
                          );
                        },
                      ),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: ElevatedButton.icon(
                            onPressed: quickPlayerState.isLoading ? null : () => ref.read(quickPlayerProvider.notifier).playQueue(),
                            icon: const Icon(Icons.play_circle_filled, size: 16),
                            label: Text('تشغيل القائمة', style: GoogleFonts.cairo(fontSize: 11, fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: const Color(0xFF00F2FE),
                              foregroundColor: Colors.white,
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                            ),
                          ),
                        ),
                        const SizedBox(width: 8),
                        ElevatedButton(
                          onPressed: () => ref.read(quickPlayerProvider.notifier).clearPlaylist(),
                          style: ElevatedButton.styleFrom(
                            backgroundColor: Colors.redAccent.withOpacity(0.15),
                            foregroundColor: Colors.redAccent,
                            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
                          ),
                          child: Text('مسح', style: GoogleFonts.cairo(fontSize: 11, fontWeight: FontWeight.bold)),
                        ),
                      ],
                    ),
                  ],
                  const Divider(color: Colors.white10, height: 16),
                  SwitchListTile(
                    contentPadding: EdgeInsets.zero,
                    dense: true,
                    value: quickPlayerState.respectAdhan,
                    onChanged: (val) => ref.read(quickPlayerProvider.notifier).setRespectAdhan(val),
                    title: Text('إيقاف التشغيل تلقائياً عند الأذان والإقامة', style: GoogleFonts.cairo(color: Colors.white54, fontSize: 12)),
                    activeColor: const Color(0xFF8E44AD),
                  ),
                ],
              ],
            ),
          ),
          const SizedBox(height: 16),

          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(color: Colors.white.withOpacity(0.08)),
              boxShadow: [
                BoxShadow(
                  color: Colors.black.withOpacity(0.05),
                  blurRadius: 15,
                  offset: const Offset(0, 8),
                ),
              ],
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.volume_up_rounded, color: Color(0xFF00F2FE), size: 20),
                        const SizedBox(width: 8),
                        Text(
                          'مستوى الصوت',
                          style: GoogleFonts.cairo(
                            fontSize: 14,
                            fontWeight: FontWeight.bold,
                            color: Colors.white,
                          ),
                        ),
                      ],
                    ),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                      decoration: BoxDecoration(
                        color: const Color(0xFF00F2FE).withOpacity(0.15),
                        borderRadius: BorderRadius.circular(10),
                        border: Border.all(color: const Color(0xFF00F2FE).withOpacity(0.3)),
                      ),
                      child: Text(
                        '${((_isDraggingVolume ? _localVolume : null) ?? status.volume.toDouble()).toInt().clamp(0, 30)}',
                        style: GoogleFonts.orbitron(
                          color: const Color(0xFF00F2FE),
                          fontSize: 13,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                Row(
                  children: [
                    IconButton(
                      icon: const Icon(Icons.remove_circle_outline_rounded, color: Colors.white60, size: 24),
                      onPressed: () async {
                        final currentVol = (_localVolume ?? status.volume.toDouble()).round();
                        if (currentVol > 0) {
                          final newVol = currentVol - 1;
                          setState(() {
                            _localVolume = newVol.toDouble();
                            _isDraggingVolume = true;
                          });
                          _volumeDebounceTimer?.cancel();
                          await ref.read(apiServiceProvider).setVolume(newVol);
                          ref.read(statusPollingProvider.notifier).pollStatus();
                          Future.delayed(const Duration(milliseconds: 800), () {
                            if (mounted) {
                              setState(() {
                                _isDraggingVolume = false;
                                _localVolume = null;
                              });
                            }
                          });
                        }
                      },
                    ),
                    Expanded(
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: const Color(0xFF00F2FE),
                          inactiveTrackColor: Colors.white.withOpacity(0.1),
                          thumbColor: const Color(0xFF00F2FE),
                          overlayColor: const Color(0xFF00F2FE).withOpacity(0.2),
                          valueIndicatorColor: const Color(0xFF00F2FE),
                          trackHeight: 6,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 8),
                          overlayShape: const RoundSliderOverlayShape(overlayRadius: 18),
                        ),
                        child: Slider(
                          value: ((_isDraggingVolume ? _localVolume : null) ?? status.volume.toDouble()).clamp(0.0, 30.0),
                          min: 0,
                          max: 30,
                          onChanged: (value) {
                            setState(() {
                              _isDraggingVolume = true;
                              _localVolume = value;
                            });
                            _volumeDebounceTimer?.cancel();
                            _volumeDebounceTimer = Timer(const Duration(milliseconds: 180), () {
                              ref.read(apiServiceProvider).setVolume(value.round());
                            });
                          },
                          onChangeEnd: (value) async {
                            _volumeDebounceTimer?.cancel();
                            final targetVol = value.round();
                            setState(() {
                              _localVolume = targetVol.toDouble();
                            });
                            await ref.read(apiServiceProvider).setVolume(targetVol);
                            ref.read(statusPollingProvider.notifier).pollStatus();
                            Future.delayed(const Duration(milliseconds: 800), () {
                              if (mounted) {
                                setState(() {
                                  _isDraggingVolume = false;
                                  _localVolume = null;
                                });
                              }
                            });
                          },
                        ),
                      ),
                    ),
                    IconButton(
                      icon: const Icon(Icons.add_circle_outline_rounded, color: Color(0xFF00F2FE), size: 24),
                      onPressed: () async {
                        final currentVol = (_localVolume ?? status.volume.toDouble()).round();
                        if (currentVol < 30) {
                          final newVol = currentVol + 1;
                          setState(() {
                            _localVolume = newVol.toDouble();
                            _isDraggingVolume = true;
                          });
                          _volumeDebounceTimer?.cancel();
                          await ref.read(apiServiceProvider).setVolume(newVol);
                          ref.read(statusPollingProvider.notifier).pollStatus();
                          Future.delayed(const Duration(milliseconds: 800), () {
                            if (mounted) {
                              setState(() {
                                _isDraggingVolume = false;
                                _localVolume = null;
                              });
                            }
                          });
                        }
                      },
                    ),
                  ],
                ),
                const SizedBox(height: 16),
                SizedBox(
                  width: double.infinity,
                  child: ElevatedButton.icon(
                    onPressed: () async {
                      final success = await ref.read(apiServiceProvider).stopAudio();
                      if (context.mounted) {
                        ScaffoldMessenger.of(context).showSnackBar(
                          SnackBar(
                            content: Text(success ? 'تم إيقاف تشغيل الصوت' : 'فشل إيقاف الصوت'),
                            backgroundColor: success ? Colors.green : Colors.redAccent,
                          ),
                        );
                      }
                    },
                    icon: const Icon(Icons.stop, size: 20),
                    label: Text(
                      'إيقاف الصوت',
                      style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 13),
                    ),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.redAccent.withOpacity(0.15),
                      foregroundColor: Colors.redAccent,
                      side: BorderSide(color: Colors.redAccent.withOpacity(0.3)),
                      padding: const EdgeInsets.symmetric(vertical: 12),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    ),
  );
}

  // Prayer Tab Widget
  Widget _buildPrayerTab(DeviceStatusModel status) {
    if (!_prayerConfigInitialized) {
      _selectedPrayerSource = _translatePrayerSource(status.prayerSource);
      final prayers = status.prayerTimes;
      if (prayers.containsKey('الفجر')) _manualPrayerControllers['fajr']!.text = prayers['الفجر']!;
      if (prayers.containsKey('الظهر')) _manualPrayerControllers['dhuhr']!.text = prayers['الظهر']!;
      if (prayers.containsKey('العصر')) _manualPrayerControllers['asr']!.text = prayers['العصر']!;
      if (prayers.containsKey('المغرب')) _manualPrayerControllers['maghrib']!.text = prayers['المغرب']!;
      if (prayers.containsKey('العشاء')) _manualPrayerControllers['isha']!.text = prayers['العشاء']!;
      _prayerConfigInitialized = true;
    }

    final prayers = status.prayerTimes;
    final keys = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'];
    
    Color sourceColor;
    switch (_translatePrayerSource(status.prayerSource)) {
      case 'روزمانة':
        sourceColor = const Color(0xFF00F2FE);
        break;
      case 'انترنت':
        sourceColor = Colors.greenAccent;
        break;
      case 'يدوي':
        sourceColor = const Color(0xFF8E44AD);
        break;
      case 'محلي':
        sourceColor = Colors.orangeAccent;
        break;
      default:
        sourceColor = Colors.white54;
    }

    return RefreshIndicator(
      color: const Color(0xFF00F2FE),
      backgroundColor: const Color(0xFF1C132E),
      onRefresh: () => _handleRefreshAll(context),
      child: SingleChildScrollView(
        physics: const AlwaysScrollableScrollPhysics(),
        padding: const EdgeInsets.all(20.0),
        child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
            decoration: BoxDecoration(
              color: Colors.white.withOpacity(0.03),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withOpacity(0.06)),
            ),
            child: Row(
              children: [
                Expanded(
                  child: Text(
                    'مصدر مواقيت الصلاة الحالية:',
                    style: GoogleFonts.cairo(
                      color: const Color(0xFFADCCFF),
                      fontSize: 14,
                      fontWeight: FontWeight.bold,
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                const SizedBox(width: 8),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 6),
                  decoration: BoxDecoration(
                    color: sourceColor.withOpacity(0.1),
                    borderRadius: BorderRadius.circular(12),
                    border: Border.all(color: sourceColor.withOpacity(0.3), width: 1),
                    boxShadow: [
                      BoxShadow(
                        color: sourceColor.withOpacity(0.05),
                        blurRadius: 8,
                      ),
                    ],
                  ),
                  child: Text(
                    _translatePrayerSource(status.prayerSource),
                    style: GoogleFonts.cairo(
                      color: sourceColor,
                      fontSize: 13,
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          Text(
            'مواقيت الصلاة لليوم',
            style: GoogleFonts.cairo(
              color: Colors.white,
              fontSize: 16,
              fontWeight: FontWeight.bold,
            ),
          ),
          const SizedBox(height: 12),

          if (prayers.isEmpty)
            Container(
              padding: const EdgeInsets.all(40),
              decoration: BoxDecoration(
                color: Colors.white.withOpacity(0.02),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Center(
                child: Column(
                  children: [
                    const CircularProgressIndicator(color: Color(0xFF00F2FE)),
                    const SizedBox(height: 16),
                    Text(
                      'جاري جلب مواقيت الصلاة من الجهاز...',
                      style: GoogleFonts.cairo(color: Colors.white38),
                    ),
                  ],
                ),
              ),
            )
          else
            ListView.separated(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: keys.length,
              separatorBuilder: (context, index) => const SizedBox(height: 10),
              itemBuilder: (context, index) {
                final name = keys[index];
                final timeVal = prayers[name] ?? '--:--';
                final isNext = (name == status.nextPrayerName);

                return Container(
                  padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 16),
                  decoration: BoxDecoration(
                    color: isNext ? const Color(0xFFF1C40F).withOpacity(0.08) : Colors.white.withOpacity(0.02),
                    borderRadius: BorderRadius.circular(16),
                    border: Border.all(
                      color: isNext ? const Color(0xFFF1C40F).withOpacity(0.4) : Colors.white.withOpacity(0.04),
                      width: isNext ? 1.5 : 1,
                    ),
                    boxShadow: isNext
                        ? [
                            BoxShadow(
                              color: const Color(0xFFF1C40F).withOpacity(0.05),
                              blurRadius: 10,
                            ),
                          ]
                        : null,
                  ),
                  child: Row(
                    children: [
                      Icon(
                        _getPrayerIcon(name),
                        color: isNext ? const Color(0xFFF1C40F) : Colors.white60,
                        size: 20,
                      ),
                      const SizedBox(width: 16),
                      Text(
                        name,
                        style: GoogleFonts.cairo(
                          color: isNext ? Colors.white : Colors.white70,
                          fontSize: 15,
                          fontWeight: isNext ? FontWeight.w900 : FontWeight.bold,
                        ),
                      ),
                      const Spacer(),
                      if (isNext) ...[
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                          decoration: BoxDecoration(
                            color: const Color(0xFFF1C40F).withOpacity(0.15),
                            borderRadius: BorderRadius.circular(6),
                          ),
                          child: Text(
                            'التالية',
                            style: GoogleFonts.cairo(
                              color: const Color(0xFFF1C40F),
                              fontSize: 10,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                        ),
                        const SizedBox(width: 12),
                      ],
                      Text(
                        _formatTime(timeVal, ref.watch(timeFormatProvider)),
                        style: GoogleFonts.orbitron(
                          color: isNext ? const Color(0xFFF1C40F) : Colors.white,
                          fontSize: 16,
                          fontWeight: isNext ? FontWeight.bold : FontWeight.normal,
                          letterSpacing: 1,
                        ),
                      ),
                    ],
                  ),
                );
              },
            ),
            const SizedBox(height: 20),
            
            StatefulBuilder(
              builder: (context, setPrayerState) => Container(
                padding: const EdgeInsets.all(20),
                decoration: BoxDecoration(
                  color: const Color(0x7316132C),
                  borderRadius: BorderRadius.circular(24),
                  border: Border.all(color: Colors.white.withOpacity(0.08)),
                  boxShadow: [
                    BoxShadow(
                      color: Colors.black.withOpacity(0.05),
                      blurRadius: 15,
                      offset: const Offset(0, 8),
                    ),
                  ],
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.settings_outlined, color: Color(0xFF00F2FE), size: 20),
                        const SizedBox(width: 10),
                        Expanded(
                          child: Text(
                            'إعدادات ومصدر مواقيت الصلاة',
                            style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                      ],
                    ),
                    const Divider(color: Colors.white10, height: 24),

                    Text(
                      'الموقع الجغرافي (البلد والمدينة):',
                      style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 8),
                    Row(
                      children: [
                        Expanded(
                          child: DropdownButtonFormField<String>(
                            isExpanded: true,
                            dropdownColor: const Color(0xFF1C132E),
                            style: const TextStyle(color: Colors.white, fontSize: 13),
                            value: _countries.contains(_selectedCountry) ? _selectedCountry : null,
                            hint: Text('اختر البلد', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12)),
                            decoration: InputDecoration(
                              filled: true,
                              fillColor: Colors.black26,
                              contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                              border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                            ),
                            items: _countries.map((c) => DropdownMenuItem<String>(value: c, child: Text(c, overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                            onChanged: _loadingLocation ? null : (val) async {
                              setPrayerState(() {
                                _loadingLocation = true;
                              });
                              await _onCountryChanged(val);
                              setPrayerState(() {
                                _loadingLocation = false;
                              });
                            },
                          ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: DropdownButtonFormField<String>(
                            isExpanded: true,
                            dropdownColor: const Color(0xFF1C132E),
                            style: const TextStyle(color: Colors.white, fontSize: 13),
                            value: _cities.contains(_selectedCity) ? _selectedCity : null,
                            hint: Text('اختر المدينة', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12)),
                            decoration: InputDecoration(
                              filled: true,
                              fillColor: Colors.black26,
                              contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                              border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                            ),
                            items: _cities.map((c) => DropdownMenuItem<String>(value: c, child: Text(c, overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                            onChanged: _loadingLocation ? null : (val) {
                              setPrayerState(() {
                                _selectedCity = val;
                              });
                              setState(() {
                                _selectedCity = val;
                              });
                            },
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    SizedBox(
                      width: double.infinity,
                      child: ElevatedButton.icon(
                        onPressed: (_loadingLocation || _selectedCountry == null || _selectedCity == null) ? null : () async {
                          setPrayerState(() {
                            _loadingLocation = true;
                          });
                          final success = await ref.read(apiServiceProvider).saveLocation(_selectedCountry!, _selectedCity!);
                          setPrayerState(() {
                            _loadingLocation = false;
                          });
                          if (context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text(success ? 'تم حفظ الموقع الجغرافي بنجاح' : 'فشل حفظ الموقع الجغرافي'),
                                backgroundColor: success ? Colors.green : Colors.redAccent,
                              ),
                            );
                          }
                          ref.read(statusPollingProvider.notifier).pollStatus();
                        },
                        icon: const Icon(Icons.location_on, size: 16),
                        label: Text('حفظ الموقع والبلد', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: const Color(0xFF00F2FE).withOpacity(0.15),
                          foregroundColor: const Color(0xFF00F2FE),
                          side: BorderSide(color: const Color(0xFF00F2FE).withOpacity(0.3)),
                          padding: const EdgeInsets.symmetric(vertical: 10),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ),
                    const Divider(color: Colors.white10, height: 24),
                    
                    Text(
                      'مصدر مواقيت الصلاة:',
                      style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 8),
                    DropdownButtonFormField<String>(
                      isExpanded: true,
                      dropdownColor: const Color(0xFF1C132E),
                      style: const TextStyle(color: Colors.white, fontSize: 13),
                      value: _selectedPrayerSource,
                      decoration: InputDecoration(
                        filled: true,
                        fillColor: Colors.black26,
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                      ),
                      items: const [
                        DropdownMenuItem(value: 'روزمانة', child: Text('روزمانة (تقويم محلي)', overflow: TextOverflow.ellipsis)),
                        DropdownMenuItem(value: 'انترنت', child: Text('انترنت (جلب تلقائي)', overflow: TextOverflow.ellipsis)),
                        DropdownMenuItem(value: 'يدوي', child: Text('توقيت يدوي', overflow: TextOverflow.ellipsis)),
                      ],
                      onChanged: (val) {
                        setPrayerState(() {
                          _selectedPrayerSource = val ?? 'روزمانة';
                        });
                        setState(() {
                          _selectedPrayerSource = val ?? 'روزمانة';
                        });
                      },
                    ),
                    const SizedBox(height: 16),

                    if (_selectedPrayerSource == 'يدوي') ...[
                      Text(
                        'إدخال المواقيت اليدوية (صيغة 24 ساعة، مثال: 13:45):',
                        style: GoogleFonts.cairo(color: Colors.amberAccent, fontSize: 12, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 10),
                      ...['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'].map((pName) {
                        final key = pName == 'الفجر' ? 'fajr' : pName == 'الظهر' ? 'dhuhr' : pName == 'العصر' ? 'asr' : pName == 'المغرب' ? 'maghrib' : 'isha';
                        return Padding(
                          padding: const EdgeInsets.only(bottom: 10),
                          child: Row(
                            children: [
                              SizedBox(
                                width: 80,
                                child: Text(pName, style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                              ),
                              Expanded(
                                child: SizedBox(
                                  height: 40,
                                  child: TextField(
                                    controller: _manualPrayerControllers[key],
                                    style: const TextStyle(color: Colors.white, fontSize: 13),
                                    keyboardType: TextInputType.datetime,
                                    decoration: InputDecoration(
                                      filled: true,
                                      fillColor: Colors.black12,
                                      contentPadding: const EdgeInsets.symmetric(horizontal: 10),
                                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide.none),
                                    ),
                                  ),
                                ),
                              ),
                            ],
                          ),
                        );
                      }),
                      const SizedBox(height: 12),
                      ElevatedButton.icon(
                        onPressed: () async {
                          final times = <String, String>{};
                          _manualPrayerControllers.forEach((k, ctrl) {
                            times[k] = ctrl.text.trim();
                          });
                          final success = await ref.read(apiServiceProvider).saveManualPrayers(times);
                          if (context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text(success ? 'تم حفظ المواقيت اليدوية بنجاح' : 'فشل حفظ المواقيت اليدوية'),
                                backgroundColor: success ? Colors.green : Colors.redAccent,
                              ),
                            );
                          }
                          ref.read(statusPollingProvider.notifier).pollStatus();
                        },
                        icon: const Icon(Icons.save, size: 16),
                        label: Text('حفظ المواقيت اليدوية', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: const Color(0xFF00F2FE),
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ] else if (_selectedPrayerSource == 'روزمانة') ...[
                      Text(
                        'إعدادات حساب الروزمانة:',
                        style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontSize: 12, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 10),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(
                            child: Text(
                              'تعديل التقويم الهجري (يوم):',
                              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          const SizedBox(width: 8),
                          DropdownButton<int>(
                            dropdownColor: const Color(0xFF1C132E),
                            value: _hijriOffset,
                            style: const TextStyle(color: Colors.white, fontSize: 13),
                            items: const [
                              DropdownMenuItem(value: -2, child: Text('-2 يوم')),
                              DropdownMenuItem(value: -1, child: Text('-1 يوم')),
                              DropdownMenuItem(value: 0, child: Text('0 (تلقائي)')),
                              DropdownMenuItem(value: 1, child: Text('+1 يوم')),
                              DropdownMenuItem(value: 2, child: Text('+2 يوم')),
                            ],
                            onChanged: (val) {
                              setPrayerState(() {
                                _hijriOffset = val ?? 0;
                              });
                              setState(() {
                                _hijriOffset = val ?? 0;
                              });
                            },
                          ),
                        ],
                      ),
                      const SizedBox(height: 12),
                      ElevatedButton.icon(
                        onPressed: () async {
                          if (_selectedCountry != null && _selectedCity != null) {
                            await ref.read(apiServiceProvider).saveLocation(_selectedCountry!, _selectedCity!);
                          }
                          final success = await ref.read(apiServiceProvider).savePrayerConfig('calendar', hijriOffset: _hijriOffset);
                          if (context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text(success ? 'تم حفظ إعدادات الروزمانة بنجاح' : 'فشل حفظ الإعدادات'),
                                backgroundColor: success ? Colors.green : Colors.redAccent,
                              ),
                            );
                          }
                          ref.read(statusPollingProvider.notifier).pollStatus();
                        },
                        icon: const Icon(Icons.save, size: 16),
                        label: Text('حفظ إعدادات الروزمانة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: const Color(0xFF8E44AD),
                          foregroundColor: Colors.white,
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ] else ...[
                      Text(
                        'إعدادات الحساب الرياضي (إنترنت):',
                        style: GoogleFonts.cairo(color: Colors.greenAccent, fontSize: 12, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 10),
                      Text(
                        'طريقة الحساب المعتمدة:',
                        style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                      ),
                      const SizedBox(height: 6),
                      DropdownButtonFormField<String>(
                        isExpanded: true,
                        dropdownColor: const Color(0xFF1C132E),
                        value: _calcMethod,
                        style: const TextStyle(color: Colors.white, fontSize: 13),
                        decoration: InputDecoration(
                          filled: true,
                          fillColor: Colors.black26,
                          contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                        ),
                        items: const [
                          DropdownMenuItem(value: 'UmmAlQura', child: Text('أم القرى (مكة المكرمة)', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'MWL', child: Text('رابطة العالم الإسلامي', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'ISNA', child: Text('الجمعية الإسلامية لأمريكا الشمالية', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'Egypt', child: Text('الهيئة المصرية العامة للمساحة', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'Karachi', child: Text('جامعة العلوم الإسلامية بكراتشي', overflow: TextOverflow.ellipsis)),
                        ],
                        onChanged: (val) {
                          setPrayerState(() {
                            _calcMethod = val ?? 'UmmAlQura';
                          });
                          setState(() {
                            _calcMethod = val ?? 'UmmAlQura';
                          });
                        },
                      ),
                      const SizedBox(height: 12),
                      ElevatedButton.icon(
                        onPressed: () async {
                          if (_selectedCountry != null && _selectedCity != null) {
                            await ref.read(apiServiceProvider).saveLocation(_selectedCountry!, _selectedCity!);
                          }
                          final success = await ref.read(apiServiceProvider).savePrayerConfig('internet', calcMethod: _calcMethod);
                          if (context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              SnackBar(
                                content: Text(success ? 'تم تعيين مصدر المواقيت كـ انترنت' : 'فشل تعيين المصدر'),
                                backgroundColor: success ? Colors.green : Colors.redAccent,
                              ),
                            );
                          }
                          ref.read(statusPollingProvider.notifier).pollStatus();
                        },
                        icon: const Icon(Icons.sync_outlined, size: 16),
                        label: Text('تطبيق مصدر انترنت', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: Colors.greenAccent.withOpacity(0.2),
                          foregroundColor: Colors.greenAccent,
                          padding: const EdgeInsets.symmetric(vertical: 12),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ),
            const SizedBox(height: 20),
            const _DailyOffsetCard(),
        ],
      ),
    ),
  );
}

  // --- Phase 2 Tabs Implementation ---

  // 1. Files Tab Widget
  Widget _buildFilesTab() {
    final status = ref.watch(statusPollingProvider);
    final filesState = ref.watch(filesProvider);

    return filesState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Color(0xFF00F2FE))),
      error: (err, stack) => Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline, color: Colors.redAccent, size: 48),
            const SizedBox(height: 12),
            Text('فشل تحميل الملفات: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () => ref.read(filesProvider.notifier).refresh(),
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('إعادة المحاولة', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
      data: (data) {
        final mounted = data['sd']?['connected'] ?? false;
        final totalMB = (data['sd']?['totalMB'] ?? 0).toDouble();
        final usedMB = (data['sd']?['usedMB'] ?? 0).toDouble();
        final filesList = data['files'] as List? ?? [];

        if (!mounted) {
          return Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Icon(Icons.sd_card_alert_outlined, color: Colors.amberAccent, size: 64),
                const SizedBox(height: 16),
                Text(
                  'بطاقة الذاكرة غير متصلة أو غير مهيأة',
                  style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 8),
                Text(
                  data['sd']?['error'] ?? 'الرجاء التحقق من كرت SD وإعادة تشغيل الجهاز',
                  style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12),
                ),
                const SizedBox(height: 20),
                ElevatedButton.icon(
                  onPressed: () => ref.read(filesProvider.notifier).refresh(),
                  icon: const Icon(Icons.refresh),
                  label: Text('تحديث', style: GoogleFonts.cairo()),
                  style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
                ),
              ],
            ),
          );
        }

        // Filter files by current folder directory structure
        // Files look like: "audio/fajr.mp3" or "csv/2026.csv" or flat "adhan.mp3"
        final List<Map<String, dynamic>> items = [];
        final Set<String> discoveredDirs = {};

        for (var f in filesList) {
          final String name = f['name'] ?? '';
          final int size = f['size'] ?? 0;
          final bool isDir = f['isDirectory'] ?? false;

          // Normalize current folder representation
          final folderPrefix = _currentFolder == '/' ? '' : _currentFolder.substring(1); // strip leading slash

          if (folderPrefix.isEmpty) {
            // Root items
            if (name.contains('/')) {
              final firstPart = name.split('/').first;
              discoveredDirs.add(firstPart);
            } else {
              items.add({'name': name, 'size': size, 'isDir': isDir});
            }
          } else {
            // Inside subfolder
            if (name.startsWith(folderPrefix) && name != folderPrefix) {
              final subPath = name.substring(folderPrefix.length);
              if (subPath.contains('/')) {
                final dirName = subPath.split('/').first;
                discoveredDirs.add(dirName);
              } else {
                items.add({'name': subPath, 'size': size, 'isDir': isDir});
              }
            }
          }
        }

        // Add discovered subdirs as folders
        for (var dir in discoveredDirs) {
          items.insert(0, {'name': dir, 'size': 0, 'isDir': true});
        }

        return Column(
          children: [
            // SD Storage Stats Card
            Container(
              margin: const EdgeInsets.all(16),
              padding: const EdgeInsets.all(16),
              decoration: BoxDecoration(
                color: Colors.white.withOpacity(0.03),
                borderRadius: BorderRadius.circular(16),
                border: Border.all(color: Colors.white.withOpacity(0.06)),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Icon(Icons.sd_storage, color: Color(0xFF00F2FE), size: 20),
                      const SizedBox(width: 10),
                      Expanded(
                        child: Text(
                          'مساحة بطاقة الذاكرة SD',
                          style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                      const SizedBox(width: 10),
                      Text(
                        '${usedMB.toStringAsFixed(1)} MB / ${totalMB.toStringAsFixed(1)} MB',
                        style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 12),
                      ),
                    ],
                  ),
                  const SizedBox(height: 10),
                  ClipRRect(
                    borderRadius: BorderRadius.circular(4),
                    child: LinearProgressIndicator(
                      value: totalMB > 0 ? (usedMB / totalMB) : 0,
                      backgroundColor: Colors.white10,
                      valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF00F2FE)),
                      minHeight: 6,
                    ),
                  ),
                ],
              ),
            ),

            // Inline Quick Player for Files Tab
            if (status.isPlaying || status.currentFile.isNotEmpty)
              Container(
                margin: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 8.0),
                padding: const EdgeInsets.all(14),
                decoration: BoxDecoration(
                  color: const Color(0x7316132C),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(
                    color: status.isPlaying ? const Color(0xFF00F2FE).withOpacity(0.4) : Colors.white.withOpacity(0.08),
                    width: status.isPlaying ? 1.5 : 1.0,
                  ),
                  boxShadow: [
                    BoxShadow(
                      color: status.isPlaying ? const Color(0xFF00F2FE).withOpacity(0.08) : Colors.black.withOpacity(0.05),
                      blurRadius: 15,
                      offset: const Offset(0, 6),
                    ),
                  ],
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        Icon(
                          status.isPlaying ? Icons.music_note_rounded : Icons.pause_circle_outline,
                          color: status.isPlaying ? const Color(0xFF00F2FE) : const Color(0xFFADCCFF),
                          size: 20,
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                status.currentFile.isNotEmpty ? status.currentFile : 'المشغل السريع',
                                style: GoogleFonts.cairo(
                                  fontSize: 13,
                                  fontWeight: FontWeight.bold,
                                  color: Colors.white,
                                ),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                              Text(
                                status.isPlaying ? 'قيد التشغيل من الذاكرة' : 'متوقف',
                                style: GoogleFonts.cairo(
                                  fontSize: 10,
                                  color: status.isPlaying ? const Color(0xFF00FFCC) : Colors.white38,
                                ),
                              ),
                            ],
                          ),
                        ),
                        // Stop button
                        IconButton(
                          icon: const Icon(Icons.stop_circle_rounded, color: Colors.redAccent, size: 26),
                          tooltip: 'إيقاف التشغيل',
                          onPressed: () async {
                            await ref.read(apiServiceProvider).stopAudio();
                            ref.read(quickPlayerProvider.notifier).stop();
                            ref.read(statusPollingProvider.notifier).pollStatus();
                          },
                        ),
                        // Play/Pause button
                        IconButton(
                          icon: Icon(
                            status.isPlaying ? Icons.pause_circle_filled_rounded : Icons.play_circle_filled_rounded,
                            color: const Color(0xFF00F2FE),
                            size: 28,
                          ),
                          tooltip: status.isPlaying ? 'إيقاف مؤقت' : 'تشغيل',
                          onPressed: () async {
                            if (status.isPlaying) {
                              await ref.read(apiServiceProvider).stopAudio();
                              ref.read(quickPlayerProvider.notifier).stop();
                            } else if (status.currentFile.isNotEmpty) {
                              await ref.read(filesProvider.notifier).playFile(status.currentFile);
                            }
                            ref.read(statusPollingProvider.notifier).pollStatus();
                          },
                        ),
                      ],
                    ),
                    const Divider(color: Colors.white10, height: 16),
                    // Volume row
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.volume_up_rounded, color: Color(0xFF00F2FE), size: 18),
                            const SizedBox(width: 6),
                            Text(
                              'مستوى الصوت',
                              style: GoogleFonts.cairo(fontSize: 12, fontWeight: FontWeight.bold, color: Colors.white70),
                            ),
                          ],
                        ),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                          decoration: BoxDecoration(
                            color: const Color(0xFF00F2FE).withOpacity(0.15),
                            borderRadius: BorderRadius.circular(6),
                            border: Border.all(color: const Color(0xFF00F2FE).withOpacity(0.3)),
                          ),
                          child: Text(
                            '${((_isDraggingVolume ? _localVolume : null) ?? status.volume.toDouble()).toInt().clamp(0, 30)}',
                            style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontSize: 11, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 4),
                    Row(
                      children: [
                        IconButton(
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints(),
                          icon: const Icon(Icons.remove_circle_outline_rounded, color: Colors.white60, size: 22),
                          onPressed: () async {
                            final current = (_localVolume ?? status.volume.toDouble()).round();
                            if (current > 0) {
                              final newVol = current - 1;
                              setState(() {
                                _localVolume = newVol.toDouble();
                                _isDraggingVolume = true;
                              });
                              _volumeDebounceTimer?.cancel();
                              await ref.read(apiServiceProvider).setVolume(newVol);
                              ref.read(statusPollingProvider.notifier).pollStatus();
                              Future.delayed(const Duration(milliseconds: 800), () {
                                if (mounted) {
                                  setState(() {
                                    _isDraggingVolume = false;
                                    _localVolume = null;
                                  });
                                }
                              });
                            }
                          },
                        ),
                        Expanded(
                          child: SliderTheme(
                            data: SliderTheme.of(context).copyWith(
                              activeTrackColor: const Color(0xFF00F2FE),
                              inactiveTrackColor: Colors.white.withOpacity(0.1),
                              thumbColor: const Color(0xFF00F2FE),
                              overlayColor: const Color(0xFF00F2FE).withOpacity(0.2),
                              trackHeight: 5,
                              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 7),
                              overlayShape: const RoundSliderOverlayShape(overlayRadius: 16),
                            ),
                            child: Slider(
                              value: ((_isDraggingVolume ? _localVolume : null) ?? status.volume.toDouble()).clamp(0.0, 30.0),
                              min: 0,
                              max: 30,
                              onChanged: (value) {
                                setState(() {
                                  _isDraggingVolume = true;
                                  _localVolume = value;
                                });
                                _volumeDebounceTimer?.cancel();
                                _volumeDebounceTimer = Timer(const Duration(milliseconds: 180), () {
                                  ref.read(apiServiceProvider).setVolume(value.round());
                                });
                              },
                              onChangeEnd: (value) async {
                                _volumeDebounceTimer?.cancel();
                                final targetVol = value.round();
                                setState(() {
                                  _localVolume = targetVol.toDouble();
                                });
                                await ref.read(apiServiceProvider).setVolume(targetVol);
                                ref.read(statusPollingProvider.notifier).pollStatus();
                                Future.delayed(const Duration(milliseconds: 800), () {
                                  if (mounted) {
                                    setState(() {
                                      _isDraggingVolume = false;
                                      _localVolume = null;
                                    });
                                  }
                                });
                              },
                            ),
                          ),
                        ),
                        IconButton(
                          padding: EdgeInsets.zero,
                          constraints: const BoxConstraints(),
                          icon: const Icon(Icons.add_circle_outline_rounded, color: Color(0xFF00F2FE), size: 22),
                          onPressed: () async {
                            final current = (_localVolume ?? status.volume.toDouble()).round();
                            if (current < 30) {
                              final newVol = current + 1;
                              setState(() {
                                _localVolume = newVol.toDouble();
                                _isDraggingVolume = true;
                              });
                              _volumeDebounceTimer?.cancel();
                              await ref.read(apiServiceProvider).setVolume(newVol);
                              ref.read(statusPollingProvider.notifier).pollStatus();
                              Future.delayed(const Duration(milliseconds: 800), () {
                                if (mounted) {
                                  setState(() {
                                    _isDraggingVolume = false;
                                    _localVolume = null;
                                  });
                                }
                              });
                            }
                          },
                        ),
                      ],
                    ),
                  ],
                ),
              ),

            // Directory Navigation bar
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16.0),
              child: Row(
                children: [
                  if (_currentFolder != '/') ...[
                    IconButton(
                      icon: const Icon(Icons.arrow_back, color: Colors.white),
                      onPressed: () {
                        setState(() {
                          final parts = _currentFolder.split('/');
                          if (parts.length > 2) {
                            parts.removeLast(); // remove final empty string
                            parts.removeLast(); // remove last folder name
                            _currentFolder = '${parts.join('/')}/';
                          } else {
                            _currentFolder = '/';
                          }
                        });
                      },
                    ),
                  ],
                  Expanded(
                    child: Text(
                      'المسار: $_currentFolder',
                      style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                  // Create folder button
                  IconButton(
                    icon: const Icon(Icons.create_new_folder_outlined, color: Color(0xFF00F2FE)),
                    tooltip: 'إنشاء مجلد',
                    onPressed: () => _showCreateFolderDialog(context),
                  ),
                  // Upload file button
                  IconButton(
                    icon: const Icon(Icons.cloud_upload_outlined, color: Colors.greenAccent),
                    tooltip: 'رفع ملف',
                    onPressed: () => _handleFileUpload(context),
                  ),
                ],
              ),
            ),

            // Files list
            Expanded(
              child: items.isEmpty
                  ? Center(
                      child: Text(
                        'لا توجد ملفات في هذا المجلد',
                        style: GoogleFonts.cairo(color: Colors.white30),
                      ),
                    )
                  : ListView.separated(
                      padding: const EdgeInsets.all(16),
                      itemCount: items.length,
                      separatorBuilder: (context, index) => const SizedBox(height: 8),
                      itemBuilder: (context, index) {
                        final item = items[index];
                        final String name = item['name'];
                        final bool isDir = item['isDir'];
                        final int size = item['size'];

                        // Compute full path on server for file manipulation
                        final String fullPath = _currentFolder == '/' ? name : '${_currentFolder.substring(1)}$name';

                        Widget content = Container(
                          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                          decoration: BoxDecoration(
                            color: Colors.white.withOpacity(0.01),
                            borderRadius: BorderRadius.circular(12),
                            border: Border.all(color: Colors.white.withOpacity(0.04)),
                          ),
                          child: Row(
                            children: [
                              Icon(
                                isDir ? Icons.folder : Icons.audio_file_outlined,
                                color: isDir ? const Color(0xFFF1C40F) : Colors.white60,
                                size: 22,
                              ),
                              const SizedBox(width: 12),
                              Expanded(
                                child: Column(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                    Text(
                                      name,
                                      style: GoogleFonts.cairo(
                                        color: Colors.white,
                                        fontWeight: FontWeight.bold,
                                        fontSize: 13,
                                      ),
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                    if (!isDir) ...[
                                      const SizedBox(height: 2),
                                      Text(
                                        'الحجم: ${(size / 1024).toStringAsFixed(1)} KB',
                                        style: GoogleFonts.cairo(color: Colors.white30, fontSize: 10),
                                      ),
                                    ],
                                  ],
                                ),
                              ),
                              if (isDir) ...[
                                // Open folder button
                                IconButton(
                                  icon: const Icon(Icons.arrow_forward_ios, size: 14, color: Colors.white38),
                                  onPressed: () {
                                    setState(() {
                                      _currentFolder = '$_currentFolder$name/';
                                    });
                                  },
                                ),
                              ] else ...[
                                // Play file button
                                IconButton(
                                  icon: const Icon(Icons.play_arrow, color: Colors.greenAccent),
                                  tooltip: 'تشغيل',
                                  onPressed: () async {
                                    final success = await ref.read(filesProvider.notifier).playFile(fullPath);
                                    if (context.mounted) {
                                      ScaffoldMessenger.of(context).showSnackBar(
                                        SnackBar(
                                          content: Text(success ? 'تم إرسال أمر التشغيل بنجاح' : 'فشل التشغيل'),
                                          backgroundColor: success ? Colors.green : Colors.redAccent,
                                        ),
                                      );
                                    }
                                  },
                                ),
                              ],
                              // Actions menu (Rename, Delete)
                              PopupMenuButton<String>(
                                icon: const Icon(Icons.more_vert, size: 18, color: Colors.white38),
                                color: const Color(0xFF1C132E),
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                                onSelected: (value) {
                                  if (value == 'rename') {
                                    _showRenameFileDialog(context, fullPath, name);
                                  } else if (value == 'delete') {
                                    _showDeleteFileDialog(context, fullPath, isDir);
                                  }
                                },
                                itemBuilder: (context) => [
                                  PopupMenuItem(
                                    value: 'rename',
                                    child: Row(
                                      children: [
                                        const Icon(Icons.edit, size: 16, color: const Color(0xFFADCCFF)),
                                        const SizedBox(width: 8),
                                        Text('تغيير الاسم', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                                      ],
                                    ),
                                  ),
                                  PopupMenuItem(
                                    value: 'delete',
                                    child: Row(
                                      children: [
                                        const Icon(Icons.delete, size: 16, color: Colors.redAccent),
                                        const SizedBox(width: 8),
                                        Text('حذف', style: GoogleFonts.cairo(color: Colors.redAccent, fontSize: 12)),
                                      ],
                                    ),
                                  ),
                                ],
                              ),
                            ],
                          ),
                        );
                        if (isDir) {
                          return InkWell(
                            borderRadius: BorderRadius.circular(12),
                            onTap: () {
                              setState(() {
                                _currentFolder = '$_currentFolder$name/';
                              });
                            },
                            child: content,
                          );
                        } else {
                          return content;
                        }
                      },
                    ),
            ),
          ],
        );
      },
    );
  }

  // Create folder dialog helper
  void _showCreateFolderDialog(BuildContext context) {
    final controller = TextEditingController();
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('إنشاء مجلد جديد', style: GoogleFonts.cairo(color: Colors.white)),
          content: TextField(
            controller: controller,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              hintText: 'اسم المجلد',
              hintStyle: GoogleFonts.cairo(color: Colors.white30),
              enabledBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                final name = controller.text.trim();
                if (name.isEmpty) return;
                Navigator.pop(context);
                final path = _currentFolder == '/' ? name : '${_currentFolder.substring(1)}$name';
                final success = await ref.read(filesProvider.notifier).createDirectory(path);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم إنشاء المجلد بنجاح' : 'فشل إنشاء المجلد'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('إنشاء', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  // File Rename dialog helper
  void _showRenameFileDialog(BuildContext context, String fullPath, String currentName) {
    final controller = TextEditingController(text: currentName);
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('تغيير اسم الملف', style: GoogleFonts.cairo(color: Colors.white)),
          content: TextField(
            controller: controller,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              enabledBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                final newName = controller.text.trim();
                if (newName.isEmpty || newName == currentName) return;
                Navigator.pop(context);
                final newPath = _currentFolder == '/' ? newName : '${_currentFolder.substring(1)}$newName';
                final success = await ref.read(filesProvider.notifier).renameFile(fullPath, newPath);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم تغيير الاسم بنجاح' : 'فشل تغيير الاسم'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('تغيير', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  // Delete file confirmation dialog helper
  void _showDeleteFileDialog(BuildContext context, String fullPath, bool isDir) {
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text(isDir ? 'حذف المجلد' : 'حذف الملف', style: GoogleFonts.cairo(color: Colors.white)),
          content: Text(
            isDir ? 'هل أنت متأكد من رغبتك في حذف هذا المجلد ومحتوياته؟' : 'هل أنت متأكد من رغبتك في حذف هذا الملف نهائياً؟',
            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                Navigator.pop(context);
                final success = await ref.read(filesProvider.notifier).deleteFile(fullPath);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم الحذف بنجاح' : 'فشل الحذف'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
              child: Text('حذف', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  // Pick file and upload to ESP32 with progress indicator
  Future<void> _handleFileUpload(BuildContext context) async {
    try {
      final result = await FilePicker.platform.pickFiles(
        withData: false, // Stream chunk-by-chunk from disk to prevent OOM
        type: FileType.custom,
        allowedExtensions: ['mp3', 'wav', 'csv'],
      );

      if (result != null && result.files.isNotEmpty) {
        final file = result.files.first;
        final name = file.name;
        final filePath = file.path;
        List<int>? bytes = file.bytes;

        int totalBytes = file.size;
        if (totalBytes <= 0 && filePath != null) {
          try {
            totalBytes = File(filePath).lengthSync();
          } catch (_) {}
        }
        if (totalBytes <= 0 && bytes != null) {
          totalBytes = bytes.length;
        }

        if (filePath == null && bytes == null) {
          if (context.mounted) {
            ScaffoldMessenger.of(context).showSnackBar(
              const SnackBar(content: Text('تعذر قراءة بيانات الملف المختار'), backgroundColor: Colors.redAccent),
            );
          }
          return;
        }

        double uploadProgress = 0.0;
        int sentBytes = 0;
        String speedText = '';
        String uploadStatusText = 'جاري الرفع...';
        bool isCancelledByUser = false;
        StateSetter? dialogSetState;
        bool isDialogShowing = false;

        if (context.mounted) {
          isDialogShowing = true;
          showDialog(
            context: context,
            barrierDismissible: false,
            builder: (dialogCtx) {
              return StatefulBuilder(
                builder: (context, setState) {
                  dialogSetState = setState;
                  return Directionality(
                    textDirection: TextDirection.rtl,
                    child: AlertDialog(
                      backgroundColor: const Color(0xFF1C132E),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
                      title: Row(
                        children: [
                          const Icon(Icons.cloud_upload_outlined, color: Color(0xFF00F2FE)),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Text(
                              'جاري رفع الملف إلى الذاكرة',
                              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
                            ),
                          ),
                        ],
                      ),
                      content: Column(
                        mainAxisSize: MainAxisSize.min,
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          Text(
                            name,
                            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                          const SizedBox(height: 16),
                          ClipRRect(
                            borderRadius: BorderRadius.circular(8),
                            child: LinearProgressIndicator(
                              value: uploadProgress > 0 ? uploadProgress : null,
                              backgroundColor: Colors.white10,
                              valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF00F2FE)),
                              minHeight: 10,
                            ),
                          ),
                          const SizedBox(height: 12),
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              Text(
                                '${(uploadProgress * 100).toStringAsFixed(1)}%',
                                style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold, fontSize: 13),
                              ),
                              Text(
                                totalBytes > 0
                                    ? '${(sentBytes / 1024 / 1024).toStringAsFixed(2)} / ${(totalBytes / 1024 / 1024).toStringAsFixed(2)} MB'
                                    : '',
                                style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
                              ),
                            ],
                          ),
                          if (speedText.isNotEmpty || uploadStatusText.isNotEmpty) ...[
                            const SizedBox(height: 8),
                            Row(
                              mainAxisAlignment: MainAxisAlignment.spaceBetween,
                              children: [
                                Text(
                                  uploadStatusText,
                                  style: GoogleFonts.cairo(
                                    color: uploadStatusText.contains('إعادة') ? const Color(0xFFF39C12) : const Color(0xFFADCCFF),
                                    fontSize: 11,
                                  ),
                                ),
                                if (speedText.isNotEmpty)
                                  Text(
                                    speedText,
                                    style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontSize: 11, fontWeight: FontWeight.bold),
                                  ),
                              ],
                            ),
                          ],
                        ],
                      ),
                      actions: [
                        TextButton(
                          onPressed: () {
                            isCancelledByUser = true;
                            Navigator.of(context, rootNavigator: true).pop();
                          },
                          child: Text('إلغاء الرفع', style: GoogleFonts.cairo(color: Colors.redAccent, fontWeight: FontWeight.bold)),
                        ),
                      ],
                    ),
                  );
                },
              );
            },
          ).then((_) {
            isDialogShowing = false;
          });
        }

        // Upload to active folder relative path (stripping leading slash for uploadFolder parameter)
        final serverFolder = _currentFolder == '/' ? '/' : _currentFolder.substring(0, _currentFolder.length - 1);
        final success = await ref.read(filesProvider.notifier).uploadFile(
          bytes: bytes,
          filePath: filePath,
          filename: name,
          folder: serverFolder,
          isCancelled: () => isCancelledByUser,
          onSendProgress: (sent, total, speedBps, statusMsg) {
            if (dialogSetState != null) {
              dialogSetState!(() {
                sentBytes = sent;
                totalBytes = total > 0 ? total : totalBytes;
                uploadProgress = (totalBytes > 0) ? (sent / totalBytes).clamp(0.0, 1.0) : 0.0;
                uploadStatusText = statusMsg;
                if (speedBps > 1024 * 1024) {
                  speedText = '${(speedBps / (1024 * 1024)).toStringAsFixed(1)} MB/s';
                } else if (speedBps > 0) {
                  speedText = '${(speedBps / 1024).toStringAsFixed(0)} KB/s';
                }
              });
            }
          },
        );

        if (context.mounted) {
          if (isDialogShowing) {
            Navigator.of(context, rootNavigator: true).pop(); // Close progress dialog
          }
          if (!isCancelledByUser) {
            ScaffoldMessenger.of(context).showSnackBar(
              SnackBar(
                content: Text(success ? 'تم رفع الملف بنجاح!' : 'فشل رفع الملف إلى الذاكرة'),
                backgroundColor: success ? Colors.green : Colors.redAccent,
              ),
            );
          }
        }
      }
    } catch (e) {
      if (context.mounted) {
        Navigator.of(context, rootNavigator: true).pop();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('خطأ أثناء رفع الملف: $e'), backgroundColor: Colors.redAccent),
        );
      }
    }
  }


  // 2. Alerts (Schedules) Tab Widget
  Widget _buildAlertsTab() {
    final alertsState = ref.watch(alertsProvider);

    return alertsState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Color(0xFF00F2FE))),
      error: (err, stack) => Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline, color: Colors.redAccent, size: 48),
            const SizedBox(height: 12),
            Text('فشل تحميل التنبيهات: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () => ref.read(alertsProvider.notifier).refresh(),
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('إعادة المحاولة', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
      data: (alertsList) {
        // Group and filter the alerts while keeping track of their original flat list indices
        final List<Map<String, dynamic>> audioAlerts = [];
        final List<Map<String, dynamic>> radioAlerts = [];
        final List<Map<String, dynamic>> playlistAlerts = [];

        for (int i = 0; i < alertsList.length; i++) {
          final alert = Map<String, dynamic>.from(alertsList[i]);
          alert['originalIndex'] = i;
          final file = alert['file']?.toString() ?? '';
          if (file.startsWith('http://') || file.startsWith('https://')) {
            radioAlerts.add(alert);
          } else if (file.contains(',')) {
            playlistAlerts.add(alert);
          } else {
            audioAlerts.add(alert);
          }
        }

        final List<Map<String, dynamic>> filteredAlerts;
        String emptyMessage = '';
        if (_alertsSubTabIndex == 0) {
          filteredAlerts = audioAlerts;
          emptyMessage = 'لا توجد تنبيهات ملفات صوتية مضافة حالياً';
        } else if (_alertsSubTabIndex == 1) {
          filteredAlerts = radioAlerts;
          emptyMessage = 'لا توجد تنبيهات بث مباشر وراديو مضافة حالياً';
        } else {
          filteredAlerts = playlistAlerts;
          emptyMessage = 'لا توجد تنبيهات قوائم تشغيل مضافة حالياً';
        }

        return Column(
          children: [
            // Header stats & Add Alert Button
            Padding(
              padding: const EdgeInsets.all(16.0),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      'التنبيهات المجدولة (${alertsList.length})',
                      style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                  ),
                  ElevatedButton.icon(
                    onPressed: () => _showAddEditAlertSheet(context, null, -1),
                    icon: const Icon(Icons.add, size: 18),
                    label: Text('تنبيه جديد', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF00F2FE),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ],
              ),
            ),

            // Segmented Category Tabs
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16.0, vertical: 8.0),
              child: Row(
                children: [
                  _buildAlertCategoryTab(0, 'ملفات صوتية', audioAlerts.length, Icons.audiotrack),
                  const SizedBox(width: 8),
                  _buildAlertCategoryTab(1, 'بث وراديو', radioAlerts.length, Icons.wifi_tethering),
                  const SizedBox(width: 8),
                  _buildAlertCategoryTab(2, 'قوائم تشغيل', playlistAlerts.length, Icons.playlist_play),
                ],
              ),
            ),

            // Alerts ListView
            Expanded(
              child: filteredAlerts.isEmpty
                  ? Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          const Icon(Icons.alarm_off, color: Colors.white24, size: 64),
                          const SizedBox(height: 16),
                          Text(emptyMessage, style: GoogleFonts.cairo(color: Colors.white30)),
                        ],
                      ),
                    )
                  : ListView.separated(
                      padding: const EdgeInsets.symmetric(horizontal: 16),
                      itemCount: filteredAlerts.length,
                      separatorBuilder: (context, index) => const SizedBox(height: 10),
                      itemBuilder: (context, index) {
                        final alert = filteredAlerts[index];
                        final int originalIndex = alert['originalIndex'] ?? index;
                        final String name = alert['name'] ?? '';
                        final String file = alert['file'] ?? '';
                        final String type = alert['type'] ?? 'daily';
                        final int hour = alert['hour'] ?? 0;
                        final int minute = alert['minute'] ?? 0;
                        final bool enabled = alert['enabled'] ?? true;
                        final int volume = alert['volume'] ?? 20;
                        final bool gpioActive = alert['gpioActive'] ?? false;
                        final int gpioPin = alert['gpioPin'] ?? 0;

                        // Localized types
                        String typeText = 'يومي';
                        if (type == 'weekly') typeText = 'أسبوعي';
                        if (type == 'monthly') typeText = 'شهري';
                        if (type == 'specific') typeText = 'تاريخ محدد';
                        if (type == 'prayer_relative') {
                          final pIdx = alert['prayerIndex'] ?? 0;
                          final offset = (alert['offsetSeconds'] ?? 0) ~/ 60;
                          final pName = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'][pIdx];
                          typeText = 'مرتبط بصلاة $pName (${offset >= 0 ? '+' : ''}$offset دقيقة)';
                        }

                        return Opacity(
                          opacity: enabled ? 1.0 : 0.6,
                          child: Container(
                            padding: const EdgeInsets.all(16),
                            decoration: BoxDecoration(
                              color: Colors.white.withOpacity(0.02),
                              borderRadius: BorderRadius.circular(20),
                              border: Border.all(
                                color: enabled ? const Color(0xFFBF5AF2).withOpacity(0.2) : Colors.white10,
                              ),
                              boxShadow: enabled
                                  ? [
                                      BoxShadow(
                                        color: const Color(0xFFBF5AF2).withOpacity(0.01),
                                        blurRadius: 10,
                                      ),
                                    ]
                                  : null,
                            ),
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Row(
                                  children: [
                                    Expanded(
                                      child: Text(
                                        name.isNotEmpty ? name : 'تنبيه بدون اسم',
                                        style: GoogleFonts.cairo(
                                          color: Colors.white,
                                          fontWeight: FontWeight.bold,
                                          fontSize: 14,
                                        ),
                                      ),
                                    ),
                                    Switch(
                                      value: enabled,
                                      activeColor: const Color(0xFFBF5AF2),
                                      onChanged: (value) async {
                                        await ref.read(alertsProvider.notifier).toggleAlert(originalIndex, value);
                                      },
                                    ),
                                  ],
                                ),
                                const SizedBox(height: 4),
                                Row(
                                  children: [
                                    if (type != 'prayer_relative') ...[
                                      const Icon(Icons.access_time, size: 14, color: Colors.white60),
                                      const SizedBox(width: 6),
                                      Text(
                                        '${hour.toString().padLeft(2, '0')}:${minute.toString().padLeft(2, '0')}',
                                        style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
                                      ),
                                      const SizedBox(width: 16),
                                    ],
                                    const Icon(Icons.repeat, size: 14, color: Colors.white60),
                                    const SizedBox(width: 6),
                                    (() {
                                      List<int> alertDays = [];
                                      if (alert['days'] != null) {
                                        if (alert['days'] is List) {
                                          alertDays = List<int>.from(alert['days'].map((e) => int.parse(e.toString())));
                                        } else if (alert['days'] is String) {
                                          alertDays = alert['days']
                                              .toString()
                                              .split(',')
                                              .where((s) => s.trim().isNotEmpty)
                                              .map((s) => int.parse(s.trim()))
                                              .toList();
                                        }
                                      }
                                      
                                      String daysText = '';
                                      if (type == 'weekly' || type == 'prayer_relative') {
                                        if (alertDays.length == 7) {
                                          daysText = ' (يومياً)';
                                        } else if (alertDays.isNotEmpty) {
                                          final names = alertDays.map((d) => _daysOfWeek[d]).join('، ');
                                          daysText = ' ($names)';
                                        } else {
                                          daysText = ' (لا يوجد أيام)';
                                        }
                                      }
                                      return Expanded(
                                        child: Text(
                                          '$typeText$daysText',
                                          style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                                          overflow: TextOverflow.ellipsis,
                                        ),
                                      );
                                    })(),
                                  ],
                                ),
                                const SizedBox(height: 10),
                                const Divider(color: Colors.white10, height: 1),
                                const SizedBox(height: 10),
                                Row(
                                  children: [
                                    Icon(
                                      (_alertsSubTabIndex == 1)
                                          ? Icons.wifi_tethering
                                          : (_alertsSubTabIndex == 2)
                                              ? Icons.playlist_play
                                              : Icons.audio_file_outlined,
                                      size: 14,
                                      color: Colors.white38,
                                    ),
                                    const SizedBox(width: 6),
                                    Expanded(
                                      child: Text(
                                        file,
                                        style: const TextStyle(fontFamily: 'monospace', color: Colors.white54, fontSize: 11),
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                    ),
                                    Container(
                                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                                      decoration: BoxDecoration(
                                        color: const Color(0xFF00F2FE).withOpacity(0.1),
                                        borderRadius: BorderRadius.circular(6),
                                      ),
                                      child: Text(
                                        'مستوى: $volume',
                                        style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontSize: 10, fontWeight: FontWeight.bold),
                                      ),
                                    ),
                                    if (gpioActive) ...[
                                      const SizedBox(width: 8),
                                      Container(
                                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                                        decoration: BoxDecoration(
                                          color: Colors.amberAccent.withOpacity(0.1),
                                          borderRadius: BorderRadius.circular(6),
                                        ),
                                        child: Text(
                                          'GPIO: $gpioPin',
                                          style: GoogleFonts.cairo(color: Colors.amberAccent, fontSize: 10, fontWeight: FontWeight.bold),
                                        ),
                                      ),
                                    ],
                                  ],
                                ),
                                const SizedBox(height: 12),
                                Row(
                                  mainAxisAlignment: MainAxisAlignment.end,
                                  children: [
                                    TextButton.icon(
                                      onPressed: () async {
                                        final success = await ref.read(alertsProvider.notifier).toggleAlert(originalIndex, !enabled);
                                        if (context.mounted) {
                                          ScaffoldMessenger.of(context).showSnackBar(
                                            SnackBar(
                                              content: Text(success 
                                                  ? (!enabled ? 'تم تشغيل التنبيه بنجاح' : 'تم إيقاف التنبيه بنجاح') 
                                                  : 'فشل تعديل حالة التنبيه'),
                                              backgroundColor: success ? Colors.green : Colors.redAccent,
                                            ),
                                          );
                                        }
                                      },
                                      icon: Icon(
                                        enabled ? Icons.pause_circle_outline : Icons.play_circle_outline,
                                        size: 16,
                                        color: enabled ? Colors.orangeAccent : Colors.greenAccent,
                                      ),
                                      label: Text(
                                        enabled ? 'إيقاف' : 'تشغيل',
                                        style: GoogleFonts.cairo(
                                          fontSize: 12,
                                          fontWeight: FontWeight.bold,
                                          color: enabled ? Colors.orangeAccent : Colors.greenAccent,
                                        ),
                                      ),
                                    ),
                                    const SizedBox(width: 12),
                                    TextButton.icon(
                                      onPressed: () => _showAddEditAlertSheet(context, alert, originalIndex),
                                      icon: const Icon(Icons.edit_outlined, size: 16),
                                      label: Text('تعديل', style: GoogleFonts.cairo(fontSize: 12)),
                                      style: TextButton.styleFrom(foregroundColor: Colors.white70),
                                    ),
                                    const SizedBox(width: 12),
                                    TextButton.icon(
                                      onPressed: () => _showDeleteAlertConfirm(context, originalIndex),
                                      icon: const Icon(Icons.delete_outline, size: 16, color: Colors.redAccent),
                                      label: Text('حذف', style: GoogleFonts.cairo(color: Colors.redAccent, fontSize: 12)),
                                    ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                        );
                      },
                    ),
            ),
          ],
        );
      },
    );
  }

  Widget _buildAlertCategoryTab(int index, String label, int count, IconData icon) {
    final isSelected = _alertsSubTabIndex == index;
    return Expanded(
      child: GestureDetector(
        onTap: () {
          setState(() {
            _alertsSubTabIndex = index;
          });
        },
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 250),
          padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 4),
          decoration: BoxDecoration(
            color: isSelected ? const Color(0xFFBF5AF2).withOpacity(0.15) : Colors.white.withOpacity(0.02),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(
              color: isSelected ? const Color(0xFFBF5AF2) : Colors.white10,
              width: 1,
            ),
            boxShadow: isSelected
                ? [
                    BoxShadow(
                      color: const Color(0xFFBF5AF2).withOpacity(0.1),
                      blurRadius: 8,
                      offset: const Offset(0, 2),
                    ),
                  ]
                : null,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                icon,
                color: isSelected ? const Color(0xFF00F2FE) : Colors.white54,
                size: 20,
              ),
              const SizedBox(height: 4),
              Text(
                label,
                style: GoogleFonts.cairo(
                  color: isSelected ? Colors.white : Colors.white70,
                  fontSize: 11,
                  fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
                ),
                textAlign: TextAlign.center,
              ),
              const SizedBox(height: 4),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                decoration: BoxDecoration(
                  color: isSelected ? const Color(0xFF00F2FE).withOpacity(0.2) : Colors.white10,
                  borderRadius: BorderRadius.circular(10),
                ),
                child: Text(
                  count.toString(),
                  style: GoogleFonts.orbitron(
                    color: isSelected ? const Color(0xFF00F2FE) : Colors.white38,
                    fontSize: 10,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  // Delete Alert dialog helper
  void _showDeleteAlertConfirm(BuildContext context, int index) {
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('حذف التنبيه المجدول', style: GoogleFonts.cairo(color: Colors.white)),
          content: Text(
            'هل أنت متأكد من رغبتك في حذف هذا التنبيه نهائياً؟',
            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                Navigator.pop(context);
                final success = await ref.read(alertsProvider.notifier).deleteAlert(index);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم حذف التنبيه بنجاح' : 'فشل حذف التنبيه'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
              child: Text('حذف', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  // Add / Edit Alert bottom sheet helper
  void _showAddEditAlertSheet(BuildContext context, Map<String, dynamic>? alert, int index, {String? initialRadioUrl, String? initialRadioName}) {
    // Available sound list from the files state provider for dropdown selection
    final filesState = ref.read(filesProvider);
    final List<String> audioFiles = [];
    filesState.whenData((data) {
      final list = data['files'] as List? ?? [];
      for (var f in list) {
        final name = f['name'] as String? ?? '';
        final isDir = f['isDirectory'] as bool? ?? false;
        if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
          audioFiles.add(name);
        }
      }
    });

    final isEdit = alert != null;
    final nameController = TextEditingController(text: isEdit ? alert['name'] : (initialRadioName != null ? 'راديو $initialRadioName' : ''));
    final String? matchedFile = _findMatchingFile(audioFiles, isEdit ? alert['file'] : '');
    final fileController = TextEditingController(text: matchedFile ?? (isEdit ? alert['file'] : (initialRadioUrl ?? '')));
    final hourController = TextEditingController(text: isEdit ? alert['hour']?.toString() : '0');
    final minController = TextEditingController(text: isEdit ? alert['minute']?.toString() : '0');
    final volumeController = TextEditingController(text: isEdit ? alert['volume']?.toString() : '20');
    final durationController = TextEditingController(text: isEdit ? alert['duration']?.toString() : '0');
    final offsetController = TextEditingController(text: isEdit ? ((alert['offsetSeconds'] ?? 0) ~/ 60).toString() : '0');
    final dateController = TextEditingController(text: isEdit ? alert['specificDate'] : '');

    String alertType = isEdit ? alert['type'] : 'daily';
    int prayerIdx = isEdit ? alert['prayerIndex'] ?? 0 : 0;
    bool gpioActive = isEdit ? alert['gpioActive'] ?? false : false;
    
    // Sanitize GPIO variables to prevent assertion failures on dropdown initialization
    int gpioPin = 4;
    if (isEdit && alert['gpioPin'] != null) {
      gpioPin = int.tryParse(alert['gpioPin'].toString()) ?? 4;
    }
    if (!const [4, 5, 6, 7, 15].contains(gpioPin)) {
      gpioPin = 4;
    }
    String gpioMode = 'continuous';
    if (isEdit && alert['gpioMode'] != null) {
      gpioMode = alert['gpioMode'].toString();
    }
    if (!const ['continuous', 'flasher', 'pulse'].contains(gpioMode)) {
      gpioMode = 'continuous';
    }
    String gpioDurMode = 'audio_duration';
    if (isEdit && alert['gpioDurationMode'] != null) {
      gpioDurMode = alert['gpioDurationMode'].toString();
    }
    if (!const ['audio_duration', 'custom'].contains(gpioDurMode)) {
      gpioDurMode = 'audio_duration';
    }

    final gpioDurSecController = TextEditingController(text: isEdit ? alert['gpioDurationSec']?.toString() : '5');
    bool important = isEdit ? alert['important'] ?? true : true;
    
    // Activation Mode setup
    int activationMode = 0; // 0: Normal, 1: Eid Only, 2: Both
    if (isEdit) {
      final bool isEid = alert['eidOnly'] == true || alert['eidOnly'] == 1 || alert['eidOnly'] == '1' || alert['eidOnly'] == 'true';
      final bool isBoth = alert['bothModes'] == true || alert['bothModes'] == 1 || alert['bothModes'] == '1' || alert['bothModes'] == 'true';
      if (isBoth) {
        activationMode = 2;
      } else if (isEid) {
        activationMode = 1;
      }
    }

    bool isRadioSource = initialRadioUrl != null || (isEdit && (alert['file']?.toString().startsWith('http://') == true || alert['file']?.toString().startsWith('https://') == true));
    
    bool skipAdhan = isEdit ? (alert['skipAdhan'] == true || alert['skipAdhan'] == 1 || alert['skipAdhan'] == '1' || alert['skipAdhan'] == 'true') : false;
    bool skipIqama = isEdit ? (alert['skipIqama'] == true || alert['skipIqama'] == 1 || alert['skipIqama'] == '1' || alert['skipIqama'] == 'true') : false;

    List<int> selectedDays = isEdit ? [] : [0, 1, 2, 3, 4, 5, 6];
    if (isEdit && alert['days'] != null) {
      if (alert['days'] is List) {
        selectedDays = List<int>.from(alert['days'].map((e) => int.parse(e.toString())));
      } else if (alert['days'] is String) {
        selectedDays = alert['days']
            .toString()
            .split(',')
            .where((s) => s.trim().isNotEmpty)
            .map((s) => int.parse(s.trim()))
            .toList();
      }
    }

    showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: const Color(0xFF1C132E),
      shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(24))),
      builder: (context) => StatefulBuilder(
        builder: (context, setSheetState) => Directionality(
          textDirection: TextDirection.rtl,
          child: Padding(
            padding: EdgeInsets.only(
              top: 24,
              left: 24,
              right: 24,
              bottom: MediaQuery.of(context).viewInsets.bottom + 24,
            ),
            child: SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    isEdit ? 'تعديل التنبيه المجدول' : 'إضافة تنبيه جديد',
                    style: GoogleFonts.cairo(color: Colors.white, fontSize: 18, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 20),

                  // Name Field
                  TextField(
                    controller: nameController,
                    style: const TextStyle(color: Colors.white),
                    decoration: InputDecoration(
                      labelText: 'اسم التنبيه',
                      labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      prefixIcon: const Icon(Icons.label_outline, color: Color(0xFFBF5AF2)),
                      filled: true,
                      fillColor: Colors.white.withOpacity(0.01),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                  const SizedBox(height: 12),

                  // Source Type Selector
                  Row(
                    children: [
                      Expanded(
                        child: ChoiceChip(
                          label: Center(child: Text('ملف محلي (SD)', style: GoogleFonts.cairo(fontSize: 12))),
                          selected: !isRadioSource,
                          onSelected: (val) {
                            setSheetState(() {
                              isRadioSource = false;
                              fileController.text = audioFiles.isNotEmpty ? audioFiles.first : '';
                            });
                          },
                          selectedColor: const Color(0xFFBF5AF2).withOpacity(0.2),
                          labelStyle: TextStyle(color: !isRadioSource ? const Color(0xFFBF5AF2) : Colors.white60),
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: ChoiceChip(
                          label: Center(child: Text('بث راديو مباشر', style: GoogleFonts.cairo(fontSize: 12))),
                          selected: isRadioSource,
                          onSelected: (val) {
                            setSheetState(() {
                              isRadioSource = true;
                              fileController.text = initialRadioUrl ?? 'http://stream.radiojar.com/8s5u5tpdtwzuv';
                            });
                          },
                          selectedColor: const Color(0xFFBF5AF2).withOpacity(0.2),
                          labelStyle: TextStyle(color: isRadioSource ? const Color(0xFFBF5AF2) : Colors.white60),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 12),

                  // File Field (with Auto selection if files exist)
                  if (!isRadioSource) ...[
                    if (audioFiles.isNotEmpty) ...[
                      DropdownButtonFormField<String>(
                        dropdownColor: const Color(0xFF1C132E),
                        value: _findMatchingFile(audioFiles, fileController.text),
                        style: const TextStyle(color: Colors.white),
                        decoration: InputDecoration(
                          labelText: 'ملف الصوت من الذاكرة',
                          labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                          prefixIcon: const Icon(Icons.audio_file_outlined, color: Color(0xFFBF5AF2)),
                          filled: true,
                          fillColor: Colors.white.withOpacity(0.01),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                        items: audioFiles.map((f) => DropdownMenuItem(value: f, child: Text(f))).toList(),
                        onChanged: (val) {
                          setSheetState(() {
                            fileController.text = val ?? '';
                          });
                        },
                      ),
                    ] else ...[
                      TextField(
                        controller: fileController,
                        style: const TextStyle(color: Colors.white),
                        decoration: InputDecoration(
                          labelText: 'اسم ملف الصوت (مثل: audio/0001.mp3)',
                          labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                          prefixIcon: const Icon(Icons.audio_file_outlined, color: Color(0xFFBF5AF2)),
                          filled: true,
                          fillColor: Colors.white.withOpacity(0.01),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ],
                  ] else ...[
                    DropdownButtonFormField<String>(
                      dropdownColor: const Color(0xFF1C132E),
                      value: const {
                        "http://stream.radiojar.com/8s5u5tpdtwzuv": "http://stream.radiojar.com/8s5u5tpdtwzuv",
                        "https://stream.radiojar.com/0tpy1h0kxtzuv": "https://stream.radiojar.com/0tpy1h0kxtzuv",
                        "https://backup.qurango.net/radio/mix": "https://backup.qurango.net/radio/mix",
                        "https://backup.qurango.net/radio/eid": "https://backup.qurango.net/radio/eid",
                        "https://backup.qurango.net/radio/roqiah": "https://backup.qurango.net/radio/roqiah",
                        "https://backup.qurango.net/radio/tafseer": "https://backup.qurango.net/radio/tafseer",
                        "https://backup.qurango.net/radio/sakeenah": "https://backup.qurango.net/radio/sakeenah",
                        "https://backup.qurango.net/radio/athkar_sabah": "https://backup.qurango.net/radio/athkar_sabah",
                        "https://backup.qurango.net/radio/athkar_masa": "https://backup.qurango.net/radio/athkar_masa",
                        "https://backup.qurango.net/radio/mishary_alafasi": "https://backup.qurango.net/radio/mishary_alafasi",
                        "https://backup.qurango.net/radio/abdulrahman_alsudaes": "https://backup.qurango.net/radio/abdulrahman_alsudaes",
                        "https://backup.qurango.net/radio/maher": "https://backup.qurango.net/radio/maher",
                        "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad": "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad",
                      }.keys.contains(fileController.text) ? fileController.text : "custom_url",
                      style: const TextStyle(color: Colors.white),
                      decoration: InputDecoration(
                        labelText: 'قناة البث الإذاعي',
                        labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                        prefixIcon: const Icon(Icons.wifi_tethering, color: Color(0xFFBF5AF2)),
                        filled: true,
                        fillColor: Colors.white.withOpacity(0.01),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                      items: const [
                        DropdownMenuItem(value: "http://stream.radiojar.com/8s5u5tpdtwzuv", child: Text("إذاعة القرآن من القاهرة")),
                        DropdownMenuItem(value: "https://stream.radiojar.com/0tpy1h0kxtzuv", child: Text("إذاعة القرآن الكريم - السعودية")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/mix", child: Text("الإذاعة العامة - تلاوات (Mp3Quran)")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/eid", child: Text("إذاعة تكبيرات العيد")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/roqiah", child: Text("إذاعة الرقية الشرعية")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/tafseer", child: Text("إذاعة تفسير القرآن الكريم")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/sakeenah", child: Text("آيات السكينة (تلاوات هادئة)")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/athkar_sabah", child: Text("أذكار الصباح (بث مباشر)")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/athkar_masa", child: Text("أذكار المساء (بث مباشر)")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/mishary_alafasi", child: Text("إذاعة مشاري العفاسي")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/abdulrahman_alsudaes", child: Text("إذاعة عبد الرحمن السديس")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/maher", child: Text("إذاعة ماهر المعيقلي")),
                        DropdownMenuItem(value: "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad", child: Text("إذاعة عبد الباسط عبد الصمد")),
                        DropdownMenuItem(value: "custom_url", child: Text("رابط بث مباشر مخصص (عنوان URL)")),
                      ],
                      onChanged: (val) {
                        setSheetState(() {
                          if (val != 'custom_url') {
                            fileController.text = val ?? '';
                          } else {
                            fileController.text = '';
                          }
                        });
                      },
                    ),
                    if (!const [
                      "http://stream.radiojar.com/8s5u5tpdtwzuv",
                      "https://stream.radiojar.com/0tpy1h0kxtzuv",
                      "https://backup.qurango.net/radio/mix",
                      "https://backup.qurango.net/radio/eid",
                      "https://backup.qurango.net/radio/roqiah",
                      "https://backup.qurango.net/radio/tafseer",
                      "https://backup.qurango.net/radio/sakeenah",
                      "https://backup.qurango.net/radio/athkar_sabah",
                      "https://backup.qurango.net/radio/athkar_masa",
                      "https://backup.qurango.net/radio/mishary_alafasi",
                      "https://backup.qurango.net/radio/abdulrahman_alsudaes",
                      "https://backup.qurango.net/radio/maher",
                      "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad",
                    ].contains(fileController.text)) ...[
                      const SizedBox(height: 12),
                      TextField(
                        controller: fileController,
                        style: const TextStyle(color: Colors.white),
                        decoration: InputDecoration(
                          labelText: 'رابط البث المباشر المخصص (URL)',
                          labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                          prefixIcon: const Icon(Icons.link, color: Color(0xFFBF5AF2)),
                          filled: true,
                          fillColor: Colors.white.withOpacity(0.01),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                    ],
                  ],
                  const SizedBox(height: 12),

                  // Type Field
                  DropdownButtonFormField<String>(
                    dropdownColor: const Color(0xFF1C132E),
                    value: alertType,
                    style: const TextStyle(color: Colors.white),
                    decoration: InputDecoration(
                      labelText: 'نوع التكرار',
                      labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      prefixIcon: const Icon(Icons.repeat, color: Color(0xFFBF5AF2)),
                      filled: true,
                      fillColor: Colors.white.withOpacity(0.01),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                    items: const [
                      DropdownMenuItem(value: 'daily', child: Text('يومي')),
                      DropdownMenuItem(value: 'weekly', child: Text('أسبوعي')),
                      DropdownMenuItem(value: 'monthly', child: Text('شهري')),
                      DropdownMenuItem(value: 'specific', child: Text('تاريخ محدد')),
                      DropdownMenuItem(value: 'prayer_relative', child: Text('مرتبط بميقات صلاة')),
                    ],
                    onChanged: (val) {
                      setSheetState(() {
                        alertType = val ?? 'daily';
                      });
                    },
                  ),
                  const SizedBox(height: 12),

                  if (alertType == 'weekly' || alertType == 'prayer_relative') ...[
                    Text(
                      'أيام التكرار الأسبوعي:',
                      style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 6),
                    Wrap(
                      spacing: 8,
                      runSpacing: 4,
                      children: List.generate(7, (dayIdx) {
                        final dayName = _daysOfWeek[dayIdx];
                        final isSelected = selectedDays.contains(dayIdx);
                        return FilterChip(
                          label: Text(
                            dayName,
                            style: GoogleFonts.cairo(
                              fontSize: 11,
                              color: isSelected ? Colors.white : Colors.white60,
                              fontWeight: isSelected ? FontWeight.bold : FontWeight.normal,
                            ),
                          ),
                          selected: isSelected,
                          selectedColor: const Color(0xFFBF5AF2),
                          checkmarkColor: Colors.white,
                          backgroundColor: Colors.white.withOpacity(0.04),
                          onSelected: (selected) {
                            setSheetState(() {
                              if (selected) {
                                selectedDays.add(dayIdx);
                              } else {
                                selectedDays.remove(dayIdx);
                              }
                            });
                          },
                        );
                      }),
                    ),
                    const SizedBox(height: 12),
                  ],

                  // Display fields conditionally based on alertType
                  if (alertType == 'prayer_relative') ...[
                    // Prayer index selection
                    DropdownButtonFormField<int>(
                      dropdownColor: const Color(0xFF1C132E),
                      value: prayerIdx,
                      style: const TextStyle(color: Colors.white),
                      decoration: InputDecoration(
                        labelText: 'اختر الميقات المرتبط',
                        labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                        filled: true,
                        fillColor: Colors.white.withOpacity(0.01),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                      items: const [
                        DropdownMenuItem(value: 0, child: Text('الفجر')),
                        DropdownMenuItem(value: 1, child: Text('الظهر')),
                        DropdownMenuItem(value: 2, child: Text('العصر')),
                        DropdownMenuItem(value: 3, child: Text('المغرب')),
                        DropdownMenuItem(value: 4, child: Text('العشاء')),
                      ],
                      onChanged: (val) {
                        setSheetState(() {
                          prayerIdx = val ?? 0;
                        });
                      },
                    ),
                    const SizedBox(height: 12),
                    // Offset minutes
                    TextField(
                      controller: offsetController,
                      style: const TextStyle(color: Colors.white),
                      keyboardType: TextInputType.number,
                      decoration: InputDecoration(
                        labelText: 'الإزاحة بالدقائق (مثال: +15 للتشغيل بعد الميقات، -10 للتشغيل قبله)',
                        labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                        filled: true,
                        fillColor: Colors.white.withOpacity(0.01),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                    ),
                  ] else ...[
                    // Hour / Minute Fields
                    Row(
                      children: [
                        Expanded(
                          child: TextField(
                            controller: hourController,
                            style: const TextStyle(color: Colors.white),
                            keyboardType: TextInputType.number,
                            decoration: InputDecoration(
                              labelText: 'الساعة (0-23)',
                              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                              filled: true,
                              fillColor: Colors.white.withOpacity(0.01),
                              border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: TextField(
                            controller: minController,
                            style: const TextStyle(color: Colors.white),
                            keyboardType: TextInputType.number,
                            decoration: InputDecoration(
                              labelText: 'الدقيقة (0-59)',
                              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                              filled: true,
                              fillColor: Colors.white.withOpacity(0.01),
                              border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    if (alertType == 'specific') ...[
                      // Specific Date Field
                      TextField(
                        controller: dateController,
                        style: const TextStyle(color: Colors.white),
                        decoration: InputDecoration(
                          labelText: 'التاريخ المحدد (YYYY-MM-DD)',
                          labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                          prefixIcon: const Icon(Icons.date_range),
                          filled: true,
                          fillColor: Colors.white.withOpacity(0.01),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                        ),
                      ),
                      const SizedBox(height: 12),
                    ],
                  ],

                  // Volume Field
                  TextField(
                    controller: volumeController,
                    style: const TextStyle(color: Colors.white),
                    keyboardType: TextInputType.number,
                    decoration: InputDecoration(
                      labelText: 'مستوى الصوت للتنبيه (1-30)',
                      labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      prefixIcon: const Icon(Icons.volume_up, color: Color(0xFFBF5AF2)),
                      filled: true,
                      fillColor: Colors.white.withOpacity(0.01),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                  const SizedBox(height: 12),

                  // Loop and Mode Flags
                  TextField(
                    controller: durationController,
                    style: const TextStyle(color: Colors.white),
                    keyboardType: TextInputType.number,
                    decoration: InputDecoration(
                      labelText: 'المدة بالثواني (0 للكامل)',
                      labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      prefixIcon: const Icon(Icons.timer_outlined, color: Color(0xFFBF5AF2)),
                      filled: true,
                      fillColor: Colors.white.withOpacity(0.01),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                  const SizedBox(height: 12),
                  CheckboxListTile(
                    title: Text('تنبيه هام (عدم مقاطعة التنبيهات الأخرى)', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                    value: important,
                    activeColor: const Color(0xFFBF5AF2),
                    contentPadding: EdgeInsets.zero,
                    controlAffinity: ListTileControlAffinity.leading,
                    onChanged: (val) {
                      setSheetState(() {
                        important = val ?? true;
                      });
                    },
                  ),
                  const SizedBox(height: 12),

                  DropdownButtonFormField<int>(
                    dropdownColor: const Color(0xFF1C132E),
                    value: activationMode,
                    style: const TextStyle(color: Colors.white, fontSize: 13),
                    decoration: InputDecoration(
                      labelText: 'وضع التفعيل (التشغيل في العيد)',
                      labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                      prefixIcon: const Icon(Icons.toggle_on_outlined, color: Color(0xFFBF5AF2)),
                      filled: true,
                      fillColor: Colors.white.withOpacity(0.01),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                    items: const [
                      DropdownMenuItem(value: 0, child: Text('الوضع الطبيعي فقط (غير مفعل في العيد)')),
                      DropdownMenuItem(value: 1, child: Text('وضع العيد فقط')),
                      DropdownMenuItem(value: 2, child: Text('كلا الوضعين (الوضع الطبيعي والعيد)')),
                    ],
                    onChanged: (val) {
                      setSheetState(() {
                        activationMode = val ?? 0;
                      });
                    },
                  ),
                  
                  if (alertType == 'prayer_relative' && isRadioSource) ...[
                    const SizedBox(height: 12),
                    Row(
                      children: [
                        Expanded(
                          child: CheckboxListTile(
                            title: Text('تخطي الأذان', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 11)),
                            value: skipAdhan,
                            activeColor: const Color(0xFFBF5AF2),
                            contentPadding: EdgeInsets.zero,
                            controlAffinity: ListTileControlAffinity.leading,
                            onChanged: (val) {
                              setSheetState(() {
                                skipAdhan = val ?? false;
                              });
                            },
                          ),
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: CheckboxListTile(
                            title: Text('تخطي الإقامة', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 11)),
                            value: skipIqama,
                            activeColor: const Color(0xFFBF5AF2),
                            contentPadding: EdgeInsets.zero,
                            controlAffinity: ListTileControlAffinity.leading,
                            onChanged: (val) {
                              setSheetState(() {
                                skipIqama = val ?? false;
                              });
                            },
                          ),
                        ),
                      ],
                    ),
                  ],
                  const SizedBox(height: 16),

                  // GPIO Trigger Section Header
                  ExpansionTile(
                    title: Row(
                      children: [
                        const Icon(Icons.settings_input_component, color: Colors.amberAccent, size: 20),
                        const SizedBox(width: 10),
                        Text('ربط تشغيل مخرج GPIO', style: GoogleFonts.cairo(color: Colors.white, fontSize: 14)),
                      ],
                    ),
                    children: [
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 8.0),
                        child: Column(
                          children: [
                            CheckboxListTile(
                              title: Text('تفعيل مخرج GPIO مع التنبيه', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
                              value: gpioActive,
                              activeColor: const Color(0xFFBF5AF2),
                              contentPadding: EdgeInsets.zero,
                              controlAffinity: ListTileControlAffinity.leading,
                              onChanged: (val) {
                                setSheetState(() {
                                  gpioActive = val ?? false;
                                });
                              },
                            ),
                            if (gpioActive) ...[
                              // Output Pin selection
                              DropdownButtonFormField<int>(
                                dropdownColor: const Color(0xFF1C132E),
                                value: gpioPin,
                                style: const TextStyle(color: Colors.white),
                                decoration: InputDecoration(
                                  labelText: 'رقم منفذ GPIO المخرج',
                                  labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                  filled: true,
                                  fillColor: Colors.white.withOpacity(0.01),
                                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                                items: const [
                                  DropdownMenuItem<int>(value: 4, child: Text('منفذ 4 (افتراضي)')),
                                  DropdownMenuItem<int>(value: 5, child: Text('منفذ 5')),
                                  DropdownMenuItem<int>(value: 6, child: Text('منفذ 6')),
                                  DropdownMenuItem<int>(value: 7, child: Text('منفذ 7')),
                                  DropdownMenuItem<int>(value: 15, child: Text('منفذ 15')),
                                ],
                                onChanged: (val) {
                                  setSheetState(() {
                                    gpioPin = val ?? 4;
                                  });
                                },
                              ),
                              const SizedBox(height: 10),
                              // Mode selection
                              DropdownButtonFormField<String>(
                                dropdownColor: const Color(0xFF1C132E),
                                value: gpioMode,
                                style: const TextStyle(color: Colors.white),
                                decoration: InputDecoration(
                                  labelText: 'نمط إشارة المخرج',
                                  labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                  filled: true,
                                  fillColor: Colors.white.withOpacity(0.01),
                                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                                items: const [
                                  DropdownMenuItem<String>(value: 'continuous', child: Text('مستمر (طوال الملف)')),
                                  DropdownMenuItem<String>(value: 'flasher', child: Text('فلاشر (فلاش متقطع)')),
                                  DropdownMenuItem<String>(value: 'pulse', child: Text('نبضة قصيرة')),
                                ],
                                onChanged: (val) {
                                  setSheetState(() {
                                    gpioMode = val ?? 'continuous';
                                  });
                                },
                              ),
                              const SizedBox(height: 10),
                              // Duration Mode selection
                              DropdownButtonFormField<String>(
                                dropdownColor: const Color(0xFF1C132E),
                                value: gpioDurMode,
                                style: const TextStyle(color: Colors.white),
                                decoration: InputDecoration(
                                  labelText: 'مدة تشغيل المخرج',
                                  labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                  filled: true,
                                  fillColor: Colors.white.withOpacity(0.01),
                                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                                items: const [
                                  DropdownMenuItem<String>(value: 'audio_duration', child: Text('مرتبط بمدة الصوت الحقيقية')),
                                  DropdownMenuItem<String>(value: 'custom', child: Text('مدة مخصصة بالثواني')),
                                ],
                                onChanged: (val) {
                                  setSheetState(() {
                                    gpioDurMode = val ?? 'audio_duration';
                                  });
                                },
                              ),
                              if (gpioDurMode == 'custom') ...[
                                const SizedBox(height: 10),
                                TextField(
                                  controller: gpioDurSecController,
                                  style: const TextStyle(color: Colors.white),
                                  keyboardType: TextInputType.number,
                                  decoration: InputDecoration(
                                    labelText: 'المدة المخصصة بالثواني',
                                    labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                    filled: true,
                                    fillColor: Colors.white.withOpacity(0.01),
                                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                                  ),
                                ),
                              ],
                            ],
                          ],
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 24),

                  // Action Buttons
                  Row(
                    children: [
                      Expanded(
                        child: TextButton(
                          onPressed: () => Navigator.pop(context),
                          child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey, fontWeight: FontWeight.bold)),
                        ),
                      ),
                      const SizedBox(width: 12),
                      Expanded(
                        child: ElevatedButton(
                          onPressed: () async {
                            final name = nameController.text.trim();
                            final file = fileController.text.trim();

                            if (file.isEmpty) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                const SnackBar(content: Text('الرجاء تعيين اسم ملف صوتي'), backgroundColor: Colors.redAccent),
                              );
                              return;
                            }

                            Navigator.pop(context);

                            // Construct payload map matching /api/scheduler/add endpoint params
                            final map = <String, dynamic>{
                              'name': name,
                              'file': file,
                              'type': alertType,
                              'volume': int.tryParse(volumeController.text) ?? 20,
                              'duration': int.tryParse(durationController.text) ?? 0,
                              'important': important ? 1 : 0,
                              'eidOnly': activationMode == 1 ? 1 : 0,
                              'bothModes': activationMode == 2 ? 1 : 0,
                              'enabled': isEdit ? (alert['enabled'] ?? true) : true,
                              'gpioActive': gpioActive ? 1 : 0,
                            };
                            if (alertType == 'weekly' || alertType == 'prayer_relative') {
                              map['days'] = selectedDays.join(',');
                            }

                            if (alertType == 'prayer_relative') {
                              map['prayerIndex'] = prayerIdx;
                              // Convert minutes back to offset seconds
                              map['offsetSeconds'] = (int.tryParse(offsetController.text) ?? 0) * 60;
                              map['skipAdhan'] = (isRadioSource && skipAdhan) ? 1 : 0;
                              map['skipIqama'] = (isRadioSource && skipIqama) ? 1 : 0;
                            } else {
                              map['hour'] = int.tryParse(hourController.text) ?? 0;
                              map['minute'] = int.tryParse(minController.text) ?? 0;
                              if (alertType == 'specific') {
                                map['specificDate'] = dateController.text.trim();
                              }
                            }

                            if (gpioActive) {
                              map['gpioPin'] = gpioPin;
                              map['gpioMode'] = gpioMode;
                              map['gpioDurationMode'] = gpioDurMode;
                              map['gpioDurationSec'] = int.tryParse(gpioDurSecController.text) ?? 5;
                            }

                            final success = await ref.read(alertsProvider.notifier).saveAlert(map, index);
                            if (context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content: Text(success ? 'تم حفظ التنبيه بنجاح' : 'فشل حفظ التنبيه'),
                                  backgroundColor: success ? Colors.green : Colors.redAccent,
                                ),
                              );
                            }
                          },
                          style: ElevatedButton.styleFrom(
                            backgroundColor: const Color(0xFF00F2FE),
                            padding: const EdgeInsets.symmetric(vertical: 14),
                            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                          ),
                          child: Text('حفظ', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                        ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }


  // 3. GPIO Tab Widget
  Widget _buildGpioTab() {
    final gpioState = ref.watch(gpioProvider);

    return gpioState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Color(0xFF00F2FE))),
      error: (err, stack) => Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline, color: Colors.redAccent, size: 48),
            const SizedBox(height: 12),
            Text('فشل تحميل بيانات GPIO: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () => ref.read(gpioProvider.notifier).refresh(),
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('إعادة المحاولة', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
      data: (stateModel) {
        final mappings = stateModel.mappings;
        final inputsList = mappings['inputs'] as List? ?? [];
        final outputsList = mappings['outputs'] as List? ?? [];
        final schedulesList = stateModel.schedules;

        return SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // Section 1: Inputs Mappings
              Row(
                children: [
                  const Icon(Icons.login, color: Color(0xFF00F2FE), size: 20),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      'مخططات المداخل (Inputs Mappings)',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.add_circle_outline, color: Color(0xFF00F2FE)),
                    onPressed: () => _showAddInputMappingDialog(context),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (inputsList.isEmpty)
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: Colors.white.withOpacity(0.01),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Center(
                    child: Text('لا توجد مخططات مداخل معينة حالياً', style: GoogleFonts.cairo(color: Colors.white30, fontSize: 12)),
                  ),
                )
              else
                ListView.separated(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  itemCount: inputsList.length,
                  separatorBuilder: (context, index) => const SizedBox(height: 8),
                  itemBuilder: (context, index) {
                    final item = inputsList[index];
                    final String name = item['name'] ?? '';
                    final int pin = item['pin'] ?? 0;
                    final String file = item['file'] ?? '';
                    final int outPin = item['outputPin'] ?? 0;
                    final int outDur = item['outputDuration'] ?? 0;

                    return Container(
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: Colors.white.withOpacity(0.02),
                        borderRadius: BorderRadius.circular(12),
                        border: Border.all(color: Colors.white.withOpacity(0.04)),
                      ),
                      child: Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  '$name (منفذ الإدخال: $pin)',
                                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                                ),
                                if (file.isNotEmpty) ...[
                                  const SizedBox(height: 4),
                                  Text(
                                    'تشغيل صوت: $file',
                                    style: const TextStyle(fontFamily: 'monospace', color: Colors.white54, fontSize: 11),
                                  ),
                                ],
                                if (outPin > 0) ...[
                                  const SizedBox(height: 4),
                                  Text(
                                    'تحفيز مخرج: $outPin (لمدة: ${outDur == -1 ? 'تبديل' : '$outDur ثانية'})',
                                    style: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
                                  ),
                                ],
                              ],
                            ),
                          ),
                          IconButton(
                            icon: const Icon(Icons.delete_outline, color: Colors.redAccent),
                            onPressed: () async {
                              final success = await ref.read(gpioProvider.notifier).deleteInputMapping(pin);
                              if (context.mounted) {
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(
                                    content: Text(success ? 'تم حذف المخطط بنجاح' : 'فشل حذف المخطط'),
                                    backgroundColor: success ? Colors.green : Colors.redAccent,
                                  ),
                                );
                              }
                            },
                          ),
                        ],
                      ),
                    );
                  },
                ),

              const SizedBox(height: 24),

              // Section 2: Outputs Mappings
              Row(
                children: [
                  const Icon(Icons.logout, color: Color(0xFFBF5AF2), size: 20),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      'مخططات المخارج المرتبطة بالأصوات',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.add_circle_outline, color: Color(0xFFBF5AF2)),
                    onPressed: () => _showAddOutputMappingDialog(context),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (outputsList.isEmpty)
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: Colors.white.withOpacity(0.01),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Center(
                    child: Text('لا توجد مخططات مخارج معينة حالياً', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12)),
                  ),
                )
              else
                ListView.separated(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  itemCount: outputsList.length,
                  separatorBuilder: (context, index) => const SizedBox(height: 8),
                  itemBuilder: (context, index) {
                    final item = outputsList[index];
                    final int pin = item['pin'] ?? 0;
                    final String alert = item['alert'] ?? '';
                    final int duration = item['duration'] ?? 5;

                    return Container(
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: Colors.white.withOpacity(0.02),
                        borderRadius: BorderRadius.circular(12),
                        border: Border.all(color: Colors.white.withOpacity(0.04)),
                      ),
                      child: Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  'منفذ الإخراج: $pin',
                                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                                ),
                                const SizedBox(height: 4),
                                Text(
                                  'يتحفز عند تشغيل: $alert',
                                  style: const TextStyle(fontFamily: 'monospace', color: Colors.white54, fontSize: 11),
                                ),
                                const SizedBox(height: 2),
                                Text(
                                  'مدة التشغيل الافتراضية: $duration ثانية',
                                  style: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
                                ),
                              ],
                            ),
                          ),
                          // Output mapping deletion is not natively exposed as a separate URL, 
                          // but overwriting pin with an empty alert mapping removes it, or it is managed on system.
                        ],
                      ),
                    );
                  },
                ),

              const SizedBox(height: 24),

              // Section 3: Scheduled GPIO
              Row(
                children: [
                  const Icon(Icons.calendar_today_outlined, color: Colors.amberAccent, size: 20),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      'جداول تشغيل المخارج المجدولة',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.add_circle_outline, color: Colors.amberAccent),
                    onPressed: () => _showAddGpioScheduleDialog(context, null, -1),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (schedulesList.isEmpty)
                Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: Colors.white.withOpacity(0.01),
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Center(
                    child: Text('لا توجد جداول مخارج مضافة حالياً', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12)),
                  ),
                )
              else
                ListView.separated(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  itemCount: schedulesList.length,
                  separatorBuilder: (context, index) => const SizedBox(height: 8),
                  itemBuilder: (context, index) {
                    final s = schedulesList[index];
                    final String name = s['name'] ?? '';
                    final int pin = s['pin'] ?? 0;
                    final bool state = s['state'] ?? true;
                    final String type = s['type'] ?? 'daily';
                    final int startH = s['startHour'] ?? 0;
                    final int startM = s['startMin'] ?? 0;
                    final int endH = s['endHour'] ?? 0;
                    final int endM = s['endMin'] ?? 0;

                    return Container(
                      padding: const EdgeInsets.all(12),
                      decoration: BoxDecoration(
                        color: Colors.white.withOpacity(0.02),
                        borderRadius: BorderRadius.circular(12),
                        border: Border.all(color: Colors.white.withOpacity(0.04)),
                      ),
                      child: Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  '$name (منفذ: $pin -> ${state ? 'تفعيل/HIGH' : 'إيقاف/LOW'})',
                                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                                ),
                                const SizedBox(height: 4),
                                Row(
                                  children: [
                                    const Icon(Icons.access_time, size: 12, color: Colors.white38),
                                    const SizedBox(width: 4),
                                    Text(
                                      'الوقت: ${startH.toString().padLeft(2, '0')}:${startM.toString().padLeft(2, '0')} إلى ${endH.toString().padLeft(2, '0')}:${endM.toString().padLeft(2, '0')}',
                                      style: GoogleFonts.orbitron(color: Colors.white54, fontSize: 11),
                                    ),
                                    const SizedBox(width: 12),
                                    const Icon(Icons.repeat, size: 12, color: Colors.white38),
                                    const SizedBox(width: 4),
                                    Text(
                                      type == 'daily' ? 'يومي' : type == 'weekly' ? 'أسبوعي' : 'شهري',
                                      style: GoogleFonts.cairo(color: Colors.white54, fontSize: 11),
                                    ),
                                  ],
                                ),
                              ],
                            ),
                          ),
                          IconButton(
                            icon: const Icon(Icons.delete_outline, color: Colors.redAccent),
                            onPressed: () async {
                              final success = await ref.read(gpioProvider.notifier).deleteGpioSchedule(index);
                              if (context.mounted) {
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(
                                    content: Text(success ? 'تم حذف الجدول بنجاح' : 'فشل حذف الجدول'),
                                    backgroundColor: success ? Colors.green : Colors.redAccent,
                                  ),
                                );
                              }
                            },
                          ),
                        ],
                      ),
                    );
                  },
                ),
            ],
          ),
        );
      },
    );
  }

  // Add Input Mapping dialog helper
  void _showAddInputMappingDialog(BuildContext context) {
    final nameController = TextEditingController();
    final pinController = TextEditingController(text: '8');
    final fileController = TextEditingController();
    final outPinController = TextEditingController(text: '0');
    final outDurController = TextEditingController(text: '0');
    final volumeController = TextEditingController(text: '20');

    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: Consumer(
          builder: (context, ref, child) {
            final filesState = ref.watch(filesProvider);
            final List<String> audioFiles = [];
            filesState.whenData((data) {
              final list = data['files'] as List? ?? [];
              for (var f in list) {
                final name = f['name'] as String? ?? '';
                final isDir = f['isDirectory'] as bool? ?? false;
                if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
                  audioFiles.add(name);
                }
              }
            });

            return StatefulBuilder(
              builder: (context, setDialogState) {
                return AlertDialog(
                  backgroundColor: const Color(0xFF1C132E),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                  title: Text('ربط مدخل GPIO جديد', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold)),
                  content: SingleChildScrollView(
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        TextField(
                          controller: nameController,
                          style: const TextStyle(color: Colors.white),
                          decoration: InputDecoration(hintText: 'اسم الربط/الوصف', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                        ),
                        const SizedBox(height: 10),
                        DropdownButtonFormField<int>(
                          dropdownColor: const Color(0xFF1C132E),
                          value: int.tryParse(pinController.text) ?? 8,
                          style: const TextStyle(color: Colors.white),
                          decoration: const InputDecoration(labelText: 'اختر مسمار المدخل GPIO'),
                          items: const [
                            DropdownMenuItem<int>(value: 8, child: Text('GPIO 8 (مستشعر/زر 1)')),
                            DropdownMenuItem<int>(value: 9, child: Text('GPIO 9 (مستشعر/زر 2)')),
                            DropdownMenuItem<int>(value: 14, child: Text('GPIO 14 (مستشعر/زر 3)')),
                            DropdownMenuItem<int>(value: 47, child: Text('GPIO 47 (مستشعر/زر 4)')),
                          ],
                          onChanged: (val) {
                            pinController.text = (val ?? 8).toString();
                          },
                        ),
                        const SizedBox(height: 10),
                        audioFiles.isNotEmpty
                            ? DropdownButtonFormField<String>(
                                dropdownColor: const Color(0xFF1C132E),
                                isExpanded: true,
                                value: fileController.text.isEmpty ? null : _findMatchingFile(audioFiles, fileController.text),
                                style: const TextStyle(color: Colors.white),
                                decoration: InputDecoration(
                                  labelText: 'ملف الصوت المرتبط (اختياري)',
                                  labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                  enabledBorder: const OutlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
                                  focusedBorder: const OutlineInputBorder(borderSide: BorderSide(color: Color(0xFF00F2FE))),
                                ),
                                items: [
                                  DropdownMenuItem<String>(value: '', child: Text('لا يوجد', style: GoogleFonts.cairo(color: Colors.white54))),
                                  ...audioFiles.map((f) => DropdownMenuItem<String>(value: f, child: Text(f))),
                                ],
                                onChanged: (val) {
                                  setDialogState(() {
                                    fileController.text = val ?? '';
                                  });
                                },
                              )
                            : TextField(
                                controller: fileController,
                                style: const TextStyle(color: Colors.white),
                                decoration: InputDecoration(hintText: 'ملف الصوت المرتبط (اختياري)', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                              ),
                        const SizedBox(height: 10),
                        DropdownButtonFormField<int>(
                          dropdownColor: const Color(0xFF1C132E),
                          value: int.tryParse(outPinController.text) ?? 0,
                          style: const TextStyle(color: Colors.white),
                          decoration: const InputDecoration(labelText: 'تحفيز مخرج مرتبط (GPIO)'),
                          items: const [
                            DropdownMenuItem<int>(value: 0, child: Text('لا يوجد')),
                            DropdownMenuItem<int>(value: 4, child: Text('GPIO 4')),
                            DropdownMenuItem<int>(value: 5, child: Text('GPIO 5')),
                            DropdownMenuItem<int>(value: 6, child: Text('GPIO 6')),
                            DropdownMenuItem<int>(value: 7, child: Text('GPIO 7')),
                            DropdownMenuItem<int>(value: 15, child: Text('GPIO 15')),
                          ],
                          onChanged: (val) {
                            outPinController.text = (val ?? 0).toString();
                          },
                        ),
                        const SizedBox(height: 10),
                        TextField(
                          controller: outDurController,
                          style: const TextStyle(color: Colors.white),
                          keyboardType: TextInputType.number,
                          decoration: InputDecoration(hintText: 'مدة التفعيل بالثواني (0 مستمر، -1 تبديل)', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                        ),
                      ],
                    ),
                  ),
                  actions: [
                    TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
                    ),
                    ElevatedButton(
                      onPressed: () async {
                        final name = nameController.text.trim();
                        final pin = int.tryParse(pinController.text) ?? 8;
                        final file = fileController.text.trim();
                        final outPin = int.tryParse(outPinController.text) ?? 0;
                        final outDur = int.tryParse(outDurController.text) ?? 0;

                        if (name.isEmpty) return;
                        Navigator.pop(context);

                        final success = await ref.read(gpioProvider.notifier).addInputMapping({
                          'name': name,
                          'pin': pin,
                          'file': file,
                          'playDuration': 0,
                          'repeatCount': 0,
                          'outputPin': outPin,
                          'outputDuration': outDur,
                          'volume': int.tryParse(volumeController.text) ?? 20,
                        });

                        if (context.mounted) {
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text(success ? 'تم ربط المدخل بنجاح' : 'فشل ربط المدخل'),
                              backgroundColor: success ? Colors.green : Colors.redAccent,
                            ),
                          );
                        }
                      },
                      style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
                      child: Text('ربط', style: GoogleFonts.cairo()),
                    ),
                  ],
                );
              }
            );
          }
        ),
      ),
    );
  }

  // Add Output Mapping dialog helper
  void _showAddOutputMappingDialog(BuildContext context) {
    final pinController = TextEditingController(text: '4');
    final alertController = TextEditingController();
    final durController = TextEditingController(text: '5');

    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: Consumer(
          builder: (context, ref, child) {
            final filesState = ref.watch(filesProvider);
            final List<String> audioFiles = [];
            filesState.whenData((data) {
              final list = data['files'] as List? ?? [];
              for (var f in list) {
                final name = f['name'] as String? ?? '';
                final isDir = f['isDirectory'] as bool? ?? false;
                if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
                  audioFiles.add(name);
                }
              }
            });

            if (alertController.text.isEmpty && audioFiles.isNotEmpty) {
              alertController.text = audioFiles.first;
            }

            return StatefulBuilder(
              builder: (context, setDialogState) {
                return AlertDialog(
                  backgroundColor: const Color(0xFF1C132E),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                  title: Text('تعيين مخرج صوت مرتبط', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold)),
                  content: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      DropdownButtonFormField<int>(
                        dropdownColor: const Color(0xFF1C132E),
                        value: int.tryParse(pinController.text) ?? 4,
                        style: const TextStyle(color: Colors.white),
                        decoration: const InputDecoration(labelText: 'مسمار الإخراج GPIO'),
                        items: const [
                          DropdownMenuItem<int>(value: 4, child: Text('GPIO 4')),
                          DropdownMenuItem<int>(value: 5, child: Text('GPIO 5')),
                          DropdownMenuItem<int>(value: 6, child: Text('GPIO 6')),
                          DropdownMenuItem<int>(value: 7, child: Text('GPIO 7')),
                          DropdownMenuItem<int>(value: 15, child: Text('GPIO 15')),
                        ],
                        onChanged: (val) {
                          pinController.text = (val ?? 4).toString();
                        },
                      ),
                      const SizedBox(height: 10),
                      audioFiles.isNotEmpty
                          ? DropdownButtonFormField<String>(
                              dropdownColor: const Color(0xFF1C132E),
                              isExpanded: true,
                              value: audioFiles.contains(alertController.text) ? alertController.text : audioFiles.first,
                              style: const TextStyle(color: Colors.white),
                              decoration: InputDecoration(
                                labelText: 'الملف الصوتي المحفز',
                                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                                enabledBorder: const OutlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
                                focusedBorder: const OutlineInputBorder(borderSide: BorderSide(color: Color(0xFF00F2FE))),
                              ),
                              items: audioFiles.map((f) => DropdownMenuItem<String>(value: f, child: Text(f))).toList(),
                              onChanged: (val) {
                                setDialogState(() {
                                  alertController.text = val ?? '';
                                });
                              },
                            )
                          : TextField(
                              controller: alertController,
                              style: const TextStyle(color: Colors.white),
                              decoration: InputDecoration(hintText: 'يتحفز عند تشغيل هذا الملف (الاسم فقط)', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                            ),
                      const SizedBox(height: 10),
                      TextField(
                        controller: durController,
                        style: const TextStyle(color: Colors.white),
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(hintText: 'المدة بالثواني', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                      ),
                    ],
                  ),
                  actions: [
                    TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
                    ),
                    ElevatedButton(
                      onPressed: () async {
                        final pin = int.tryParse(pinController.text) ?? 4;
                        final alert = alertController.text.trim();
                        final dur = int.tryParse(durController.text) ?? 5;

                        if (alert.isEmpty) return;
                        Navigator.pop(context);

                        final success = await ref.read(gpioProvider.notifier).addOutputMapping(pin, alert, dur);
                        if (context.mounted) {
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text(success ? 'تم تعيين المخرج بنجاح' : 'فشل تعيين المخرج'),
                              backgroundColor: success ? Colors.green : Colors.redAccent,
                            ),
                          );
                        }
                      },
                      style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
                      child: Text('تعيين', style: GoogleFonts.cairo()),
                    ),
                  ],
                );
              }
            );
          }
        ),
      ),
    );
  }

  // Add GPIO Schedule dialog helper
  void _showAddGpioScheduleDialog(BuildContext context, Map<String, dynamic>? sched, int index) {
    final nameController = TextEditingController();
    final pinController = TextEditingController(text: '4');
    final stateController = TextEditingController(text: '1'); // 1 = HIGH, 0 = LOW
    final startHController = TextEditingController(text: '0');
    final startMController = TextEditingController(text: '0');
    final endHController = TextEditingController(text: '0');
    final endMController = TextEditingController(text: '0');

    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('إضافة جدول مخرج مجدول', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold)),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: nameController,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(hintText: 'اسم الجدول/الوصف', hintStyle: GoogleFonts.cairo(color: Colors.white30)),
                ),
                const SizedBox(height: 10),
                DropdownButtonFormField<int>(
                  dropdownColor: const Color(0xFF1C132E),
                  value: 4,
                  style: const TextStyle(color: Colors.white),
                  decoration: const InputDecoration(labelText: 'مسمار الإخراج GPIO'),
                  items: const [
                    DropdownMenuItem<int>(value: 4, child: Text('GPIO 4')),
                    DropdownMenuItem<int>(value: 5, child: Text('GPIO 5')),
                    DropdownMenuItem<int>(value: 6, child: Text('GPIO 6')),
                    DropdownMenuItem<int>(value: 7, child: Text('GPIO 7')),
                    DropdownMenuItem<int>(value: 15, child: Text('GPIO 15')),
                  ],
                  onChanged: (val) {
                    pinController.text = (val ?? 4).toString();
                  },
                ),
                const SizedBox(height: 10),
                DropdownButtonFormField<int>(
                  dropdownColor: const Color(0xFF1C132E),
                  value: 1,
                  style: const TextStyle(color: Colors.white),
                  decoration: const InputDecoration(labelText: 'حالة التفعيل للمنفذ'),
                  items: const [
                    DropdownMenuItem<int>(value: 1, child: Text('تفعيل (HIGH)')),
                    DropdownMenuItem<int>(value: 0, child: Text('إيقاف (LOW)')),
                  ],
                  onChanged: (val) {
                    stateController.text = (val ?? 1).toString();
                  },
                ),
                const SizedBox(height: 10),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: startHController,
                        style: const TextStyle(color: Colors.white),
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(labelText: 'ساعة البدء', labelStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12)),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: TextField(
                        controller: startMController,
                        style: const TextStyle(color: Colors.white),
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(labelText: 'دقيقة البدء', labelStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12)),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 10),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: endHController,
                        style: const TextStyle(color: Colors.white),
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(labelText: 'ساعة الانتهاء', labelStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12)),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: TextField(
                        controller: endMController,
                        style: const TextStyle(color: Colors.white),
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(labelText: 'دقيقة الانتهاء', labelStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 12)),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                final name = nameController.text.trim();
                final pin = int.tryParse(pinController.text) ?? 4;
                final state = stateController.text == '1';
                final startH = int.tryParse(startHController.text) ?? 0;
                final startM = int.tryParse(startMController.text) ?? 0;
                final endH = int.tryParse(endHController.text) ?? 0;
                final endM = int.tryParse(endMController.text) ?? 0;

                if (name.isEmpty) return;
                Navigator.pop(context);

                final success = await ref.read(gpioProvider.notifier).addGpioSchedule({
                  'name': name,
                  'pin': pin,
                  'state': state,
                  'type': 'daily',
                  'startHour': startH,
                  'startMin': startM,
                  'endHour': endH,
                  'endMin': endM,
                }, index);

                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم حفظ الجدول المجدول بنجاح' : 'فشل حفظ الجدول المجدول'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('حفظ', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  // Helper to match prayer to icon
  IconData _getPrayerIcon(String name) {
    switch (name) {
      case 'الفجر':
        return Icons.nights_stay;
      case 'الشروق':
        return Icons.wb_sunny_outlined;
      case 'الظهر':
        return Icons.wb_sunny;
      case 'العصر':
        return Icons.filter_drama;
      case 'المغرب':
        return Icons.location_city;
      case 'العشاء':
        return Icons.brightness_3;
      default:
        return Icons.alarm;
    }
  }

  // --- Phase 3 Tab Views ---

  Widget _buildSettingsTab() {
    return SingleChildScrollView(
      padding: const EdgeInsets.all(16.0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          const _WifiCard(),
          const SizedBox(height: 20),
          const _NetworkSettingsCard(),
          const SizedBox(height: 20),
          const _ApSettingsCard(),
          const SizedBox(height: 20),
          const _DdnsSettingsCard(),
          const SizedBox(height: 20),
          const _TelegramSettingsCard(),
          const SizedBox(height: 20),
          const _ClockSettingsCard(),
          const SizedBox(height: 20),
          const _PasswordCard(),
          const SizedBox(height: 20),
          const _SystemSettingsCard(),
          const SizedBox(height: 20),
          const _OtaUpdateCard(),
          const SizedBox(height: 20),
          // Unified System Control Card
          Container(
            padding: const EdgeInsets.all(20),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(24),
              border: Border.all(color: Colors.redAccent.withOpacity(0.2)),
              boxShadow: [
                BoxShadow(
                  color: Colors.black.withOpacity(0.05),
                  blurRadius: 15,
                  offset: const Offset(0, 8),
                ),
              ],
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    const Icon(Icons.settings_power, color: Colors.redAccent, size: 22),
                    const SizedBox(width: 10),
                    Text(
                      'إدارة النظام والتشغيل',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Text(
                  'إجراءات إدارية لإعادة تشغيل الجهاز، مسح البيانات والعودة لضبط المصنع، أو إيقاف تشغيل الجهاز نهائياً.',
                  style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                ),
                const SizedBox(height: 20),
                ElevatedButton.icon(
                  onPressed: () => _showRebootConfirmDialog(context),
                  icon: const Icon(Icons.restart_alt, size: 18),
                  label: Text('إعادة تشغيل الجهاز', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.white.withOpacity(0.08),
                    foregroundColor: Colors.white,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    side: BorderSide(color: Colors.white.withOpacity(0.12)),
                  ),
                ),
                const SizedBox(height: 12),
                ElevatedButton.icon(
                  onPressed: () => _showFactoryResetConfirmDialog(context),
                  icon: const Icon(Icons.restore, size: 18),
                  label: Text('إعادة ضبط المصنع', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.orangeAccent.withOpacity(0.15),
                    foregroundColor: Colors.orangeAccent,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    side: BorderSide(color: Colors.orangeAccent.withOpacity(0.3)),
                  ),
                ),
                const SizedBox(height: 12),
                ElevatedButton.icon(
                  onPressed: () => _showShutdownConfirmDialog(context),
                  icon: const Icon(Icons.power_settings_new, size: 18),
                  label: Text('إيقاف التشغيل النهائي', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.redAccent.withOpacity(0.15),
                    foregroundColor: Colors.redAccent,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                    side: BorderSide(color: Colors.redAccent.withOpacity(0.3)),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),
          Center(
            child: Text(
              'Vivo Smart App • الإصدار v1.0.5',
              style: GoogleFonts.cairo(
                color: Colors.white38,
                fontSize: 12,
                fontWeight: FontWeight.w600,
              ),
            ),
          ),
          const SizedBox(height: 24),
        ],
      ),
    );
  }

  Widget _buildLogsTab() {
    final logsState = ref.watch(logsProvider);
    final refreshNotifier = ref.read(logsProvider.notifier);

    return logsState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Color(0xFF00F2FE))),
      error: (err, stack) => Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Icon(Icons.error_outline, color: Colors.redAccent, size: 48),
            const SizedBox(height: 12),
            Text('فشل تحميل سجل الأحداث: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
            const SizedBox(height: 16),
            ElevatedButton(
              onPressed: () => refreshNotifier.refresh(),
              style: ElevatedButton.styleFrom(
                backgroundColor: const Color(0xFF00F2FE),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
              ),
              child: Text('إعادة المحاولة', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
            ),
          ],
        ),
      ),
      data: (logsList) {
        return Column(
          children: [
            // Controls row
            Padding(
              padding: const EdgeInsets.all(16.0),
              child: Row(
                children: [
                  Expanded(
                    child: Text(
                      'سجل الأحداث (${logsList.length})',
                      style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.refresh, color: Color(0xFF00F2FE)),
                    tooltip: 'تحديث السجل',
                    onPressed: () => refreshNotifier.refresh(),
                  ),
                  const SizedBox(width: 8),
                  ElevatedButton.icon(
                    onPressed: () => _showClearLogsDialog(context),
                    icon: const Icon(Icons.delete_sweep, size: 18),
                    label: Text('مسح السجل', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.redAccent,
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ],
              ),
            ),

            // Terminal Log View
            Expanded(
              child: Container(
                margin: const EdgeInsets.only(left: 16, right: 16, bottom: 16),
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: const Color(0xFF07050C),
                  borderRadius: BorderRadius.circular(16),
                  border: Border.all(color: const Color(0xFF8E44AD).withOpacity(0.15)),
                  boxShadow: [
                    BoxShadow(
                      color: const Color(0xFF8E44AD).withOpacity(0.05),
                      blurRadius: 10,
                    ),
                  ],
                ),
                child: logsList.isEmpty
                    ? Center(
                        child: Text(
                          'سجل الأحداث � ارغ حالياً',
                          style: GoogleFonts.cairo(color: Colors.white30, fontSize: 13),
                        ),
                      )
                    : ListView.builder(
                        itemCount: logsList.length,
                        itemBuilder: (context, index) {
                          final log = logsList[index];
                          final time = log['timestamp'] ?? '';
                          final lvl = (log['level'] ?? 'INFO').toString().toUpperCase();
                          final cat = log['category'] ?? 'SYSTEM';
                          final msg = log['message'] ?? '';
                          
                          Color lvlColor = Colors.greenAccent;
                          if (lvl == 'WARN' || lvl == 'WARNING') lvlColor = Colors.amberAccent;
                          if (lvl == 'ERROR' || lvl == 'FATAL') lvlColor = Colors.redAccent;

                          return Padding(
                            padding: const EdgeInsets.symmetric(vertical: 4.0),
                            child: RichText(
                              text: TextSpan(
                                children: [
                                  TextSpan(
                                    text: '[$time] ',
                                    style: const TextStyle(fontFamily: 'monospace', color: Colors.white38, fontSize: 11),
                                  ),
                                  TextSpan(
                                    text: '$lvl ',
                                    style: TextStyle(fontFamily: 'monospace', color: lvlColor, fontWeight: FontWeight.bold, fontSize: 11),
                                  ),
                                  TextSpan(
                                    text: '[$cat]: ',
                                    style: const TextStyle(fontFamily: 'monospace', color: Color(0xFF00F2FE), fontSize: 11),
                                  ),
                                  TextSpan(
                                    text: msg,
                                    style: const TextStyle(fontFamily: 'monospace', color: const Color(0xFFADCCFF), fontSize: 11),
                                  ),
                                ],
                              ),
                            ),
                          );
                        },
                      ),
              ),
            ),
          ],
        );
      },
    );
  }

  void _showClearLogsDialog(BuildContext context) {
    final passwordController = TextEditingController();
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('تأكيد مسح سجل الأحداث', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16)),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('الرجاء إدخال كلمة مرور الدخول لمسح السجلات نهائياً:', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
              const SizedBox(height: 10),
              TextField(
                controller: passwordController,
                obscureText: true,
                style: const TextStyle(color: Colors.white),
                decoration: InputDecoration(
                  hintText: 'كلمة المرور',
                  hintStyle: GoogleFonts.cairo(color: Colors.white30),
                  enabledBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
                ),
              ),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                final password = passwordController.text.trim();
                Navigator.pop(context);

                final success = await ref.read(logsProvider.notifier).clear(password);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم مسح سجل الأحداث بنجاح' : 'فشل مسح السجلات، تأكد من كلمة المرور'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
              child: Text('مسح نهائي', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }

  String _formatSeconds(int seconds) {
    final m = seconds ~/ 60;
    final s = seconds % 60;
    return '${m.toString().padLeft(2, '0')}:${s.toString().padLeft(2, '0')}';
  }

  Widget _buildQuickPlayerTab() {
    final filesState = ref.watch(filesProvider);
    final quickPlayerState = ref.watch(quickPlayerProvider);
    final status = ref.watch(statusPollingProvider);

    return filesState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Colors.white)),
      error: (err, stack) => Center(
        child: Text(
          'حدث خطأ أثناء تحميل قائمة الملفات: $err',
          style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
        ),
      ),
      data: (data) {
        final List filesList = data['files'] as List? ?? [];
        final List<String> audioFiles = [];
        for (var f in filesList) {
          final String name = f['name'] ?? '';
          final bool isDir = f['isDirectory'] ?? false;
          if (!isDir && !name.startsWith('prayer_csv/') && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
            audioFiles.add(name);
          }
        }

        if (audioFiles.isEmpty) {
          return Center(
            child: Padding(
              padding: const EdgeInsets.all(24.0),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(Icons.music_off, color: Colors.white54, size: 64),
                  const SizedBox(height: 16),
                  Text(
                    'لا توجد مل� ات صوتية متو� رة على كرت الذاكرة',
                    style: GoogleFonts.cairo(color: Colors.white, fontSize: 16, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 8),
                  Text(
                    'الرجاء الانتقال لتبويب "مدير المل� ات" ور� ع مل� ات صوتية بصيغة mp3 أو wav أولاً.',
                    style: GoogleFonts.cairo(color: Colors.white54, fontSize: 13),
                    textAlign: TextAlign.center,
                  ),
                ],
              ),
            ),
          );
        }

        // Set default selected file if not set or invalid
        if (audioFiles.isNotEmpty) {
          final matched = _findMatchingFile(audioFiles, _selectedQuickPlayerFile);
          _selectedQuickPlayerFile = matched ?? audioFiles.first;
        } else {
          _selectedQuickPlayerFile = null;
        }

        final isPlaying = quickPlayerState.playbackState == 1;
        final isPaused = quickPlayerState.playbackState == 2;
        final isAdhan = quickPlayerState.isAdhan;
        final hasActiveMedia = quickPlayerState.duration > 0 || status.isPlaying;

        return SingleChildScrollView(
          padding: const EdgeInsets.all(16.0),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Selection Card
              Container(
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: const Color(0x7316132C),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(color: Colors.white.withOpacity(0.08)),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Text(
                      'اختر ملفاً صوتياً',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                    const SizedBox(height: 12),
                    DropdownButtonFormField<String>(
                      dropdownColor: const Color(0xFF1C132E),
                      style: const TextStyle(color: Colors.white, fontSize: 14),
                      value: _findMatchingFile(audioFiles, _selectedQuickPlayerFile),
                      decoration: InputDecoration(
                        filled: true,
                        fillColor: Colors.black12,
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                      ),
                      items: audioFiles.map((file) {
                        return DropdownMenuItem<String>(
                          value: file,
                          child: Text(file, textDirection: TextDirection.ltr),
                        );
                      }).toList(),
                      onChanged: (val) {
                        setState(() {
                          _selectedQuickPlayerFile = val;
                        });
                      },
                    ),
                    const SizedBox(height: 16),
                    Row(
                      children: [
                        Expanded(
                          child: ElevatedButton.icon(
                            onPressed: quickPlayerState.isLoading
                                ? null
                                : () async {
                                    final file = _selectedQuickPlayerFile;
                                    if (file != null) {
                                      final success = await ref.read(quickPlayerProvider.notifier).play(file);
                                      if (context.mounted) {
                                        ScaffoldMessenger.of(context).showSnackBar(
                                          SnackBar(
                                            content: Text(success ? 'تم بدء تشغيل الملف' : 'فشل تشغيل الملف'),
                                            backgroundColor: success ? Colors.green : Colors.redAccent,
                                          ),
                                        );
                                      }
                                    }
                                  },
                            icon: const Icon(Icons.play_arrow),
                            label: Text('تشغيل الآن', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: const Color(0xFF00F2FE),
                              foregroundColor: Colors.white,
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                        const SizedBox(width: 12),
                        Expanded(
                          child: ElevatedButton.icon(
                            onPressed: () {
                              final file = _selectedQuickPlayerFile;
                              if (file != null) {
                                ref.read(quickPlayerProvider.notifier).addToPlaylist(file);
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(
                                    content: Text('تمت إضافة $file إلى قائمة التشغيل المؤقتة'),
                                    backgroundColor: Colors.blueGrey,
                                    duration: const Duration(seconds: 1),
                                  ),
                                );
                              }
                            },
                            icon: const Icon(Icons.playlist_add),
                            label: Text('إضافة للقائمة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: Colors.white24,
                              foregroundColor: Colors.white,
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ),
                      ],
                    )
                  ],
                ),
              ),
              const SizedBox(height: 16),

              // Player Controls Card
              Container(
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: const Color(0x7316132C),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(color: Colors.white.withOpacity(0.08)),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.music_note, color: Color(0xFF00F2FE), size: 20),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'التحكم � ي المشغل',
                            style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                            overflow: TextOverflow.ellipsis,
                          ),
                        ),
                        if (isAdhan) ...[
                          const SizedBox(width: 8),
                          Container(
                            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                            decoration: BoxDecoration(color: Colors.amber, borderRadius: BorderRadius.circular(8)),
                            child: Text('أذان / إقامة نشط', style: GoogleFonts.cairo(color: Colors.black, fontSize: 10, fontWeight: FontWeight.bold)),
                          ),
                        ],
                      ],
                    ),
                    const Divider(color: Colors.white10, height: 24),
                    if (hasActiveMedia) ...[
                      // Time Slider / Info
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text(
                            _formatSeconds(quickPlayerState.position),
                            style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 12),
                          ),
                          Text(
                            _formatSeconds(quickPlayerState.duration),
                            style: GoogleFonts.orbitron(color: const Color(0xFFADCCFF), fontSize: 12),
                          ),
                        ],
                      ),
                      SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: const Color(0xFF00F2FE),
                          inactiveTrackColor: Colors.white.withOpacity(0.1),
                          thumbColor: const Color(0xFF00F2FE),
                          overlayColor: const Color(0xFF00F2FE).withOpacity(0.2),
                          trackHeight: 4,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                        ),
                        child: Slider(
                          value: quickPlayerState.position.toDouble().clamp(0.0, quickPlayerState.duration.toDouble()),
                          min: 0.0,
                          max: quickPlayerState.duration > 0 ? quickPlayerState.duration.toDouble() : 100.0,
                          onChanged: (val) {},
                          onChangeEnd: (val) {
                            ref.read(quickPlayerProvider.notifier).seek(val.toInt());
                          },
                        ),
                      ),
                      const SizedBox(height: 12),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          IconButton(
                            icon: const Icon(Icons.stop, color: Colors.redAccent, size: 28),
                            onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).stop(),
                          ),
                          const SizedBox(width: 12),
                          IconButton(
                            icon: const Icon(Icons.replay_10, color: Colors.white, size: 28),
                            onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).seekBackward(),
                          ),
                          const SizedBox(width: 12),
                          Container(
                            decoration: const BoxDecoration(shape: BoxShape.circle, color: Color(0xFF8E44AD)),
                            child: IconButton(
                              icon: Icon(isPlaying ? Icons.pause : Icons.play_arrow, color: Colors.white, size: 30),
                              onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).togglePlayPause(),
                            ),
                          ),
                          const SizedBox(width: 12),
                          IconButton(
                            icon: const Icon(Icons.forward_10, color: Colors.white, size: 28),
                            onPressed: isAdhan ? null : () => ref.read(quickPlayerProvider.notifier).seekForward(),
                          ),
                        ],
                      ),
                    ] else ...[
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 24.0),
                        child: Column(
                          children: [
                            Icon(Icons.play_circle_outline, color: Colors.white.withOpacity(0.3), size: 48),
                            const SizedBox(height: 12),
                            Text(
                              'لا توجد وسائط قيد التشغيل حالياً.',
                              style: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                              textAlign: TextAlign.center,
                            ),
                          ],
                        ),
                      ),
                    ],
                  ],
                ),
              ),
              const SizedBox(height: 16),

              // Queue Card
              Container(
                padding: const EdgeInsets.all(16),
                decoration: BoxDecoration(
                  color: const Color(0x7316132C),
                  borderRadius: BorderRadius.circular(20),
                  border: Border.all(color: Colors.white.withOpacity(0.08)),
                ),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.playlist_play, color: Color(0xFF8E44AD), size: 20),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'قائمة التشغيل المؤقتة',
                            style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                          ),
                        ),
                        Text(
                          '${quickPlayerState.playlist.length} ملفات',
                          style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12),
                        ),
                      ],
                    ),
                    const Divider(color: Colors.white10, height: 24),
                    if (quickPlayerState.playlist.isEmpty)
                      Padding(
                        padding: const EdgeInsets.symmetric(vertical: 20.0),
                        child: Text(
                          'القائمة فارغة. أضف ملفات من الأعلى لتكوين قائمة تشغيل مؤقتة.',
                          style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12),
                          textAlign: TextAlign.center,
                        ),
                      )
                    else ...[
                      ListView.separated(
                        shrinkWrap: true,
                        physics: const NeverScrollableScrollPhysics(),
                        itemCount: quickPlayerState.playlist.length,
                        separatorBuilder: (context, index) => const Divider(color: Colors.white10, height: 1),
                        itemBuilder: (context, index) {
                          final file = quickPlayerState.playlist[index];
                          return ListTile(
                            contentPadding: EdgeInsets.zero,
                            title: Text(file, style: const TextStyle(color: Colors.white, fontSize: 13), textDirection: TextDirection.ltr),
                            leading: CircleAvatar(
                              backgroundColor: Colors.white10,
                              radius: 14,
                              child: Text('${index + 1}', style: const TextStyle(color: Colors.white, fontSize: 11)),
                            ),
                            trailing: IconButton(
                              icon: const Icon(Icons.remove_circle_outline, color: Colors.redAccent, size: 20),
                              onPressed: () => ref.read(quickPlayerProvider.notifier).removeFromPlaylist(index),
                            ),
                          );
                        },
                      ),
                      const SizedBox(height: 16),
                      Row(
                        children: [
                          Expanded(
                            child: ElevatedButton.icon(
                              onPressed: quickPlayerState.isLoading
                                  ? null
                                  : () async {
                                      final success = await ref.read(quickPlayerProvider.notifier).playQueue();
                                      if (context.mounted) {
                                        ScaffoldMessenger.of(context).showSnackBar(
                                          SnackBar(
                                            content: Text(success ? 'تم بدء تشغيل قائمة التشغيل' : 'فشل تشغيل قائمة التشغيل'),
                                            backgroundColor: success ? Colors.green : Colors.redAccent,
                                          ),
                                        );
                                      }
                                    },
                              icon: const Icon(Icons.play_circle_filled),
                              label: Text('تشغيل القائمة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: const Color(0xFF00F2FE),
                                foregroundColor: Colors.white,
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                              ),
                            ),
                          ),
                          const SizedBox(width: 12),
                          ElevatedButton.icon(
                            onPressed: () => ref.read(quickPlayerProvider.notifier).clearPlaylist(),
                            icon: const Icon(Icons.delete_sweep),
                            label: Text('مسح', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: Colors.redAccent.withOpacity(0.2),
                              foregroundColor: Colors.redAccent,
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                            ),
                          ),
                        ],
                      ),
                      const Divider(color: Colors.white10, height: 32),

                      // Playlist controls (Volume & Respect Adhan)
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Text('مستوى صوت القائمة', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
                          Text(
                            '${quickPlayerState.volume}',
                            style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold),
                          ),
                        ],
                      ),
                      SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: const Color(0xFF00F2FE),
                          inactiveTrackColor: Colors.white.withOpacity(0.1),
                          thumbColor: const Color(0xFF00F2FE),
                          trackHeight: 4,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                        ),
                        child: Slider(
                          value: quickPlayerState.volume.toDouble(),
                          min: 0,
                          max: 30,
                          divisions: 30,
                          onChanged: (val) {
                            ref.read(quickPlayerProvider.notifier).setVolume(val.toInt());
                          },
                        ),
                      ),
                      const SizedBox(height: 12),
                      SwitchListTile(
                        contentPadding: EdgeInsets.zero,
                        value: quickPlayerState.respectAdhan,
                        onChanged: (val) => ref.read(quickPlayerProvider.notifier).setRespectAdhan(val),
                        title: Text('إيقاف التشغيل تلقائياً عند الأذان والإقامة', style: GoogleFonts.cairo(color: Colors.white, fontSize: 13)),
                        activeColor: const Color(0xFF8E44AD),
                      ),
                    ],
                  ],
                ),
              )
            ],
          ),
        );
      },
    );
  }

  Widget _buildEidTab() {
    final filesState = ref.watch(filesProvider);
    final eidStateAsync = ref.watch(eidStateProvider);

    return filesState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Colors.white)),
      error: (err, stack) => Center(child: Text('فشل تحميل الملفات: $err', style: GoogleFonts.cairo(color: Colors.white))),
      data: (filesData) {
        final List filesList = filesData['files'] as List? ?? [];
        final List<String> audioFiles = [];
        for (var f in filesList) {
          final String name = f['name'] ?? '';
          final bool isDir = f['isDirectory'] ?? false;
          if (!isDir && !name.startsWith('prayer_csv/') && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
            audioFiles.add(name);
          }
        }

        if (audioFiles.isEmpty) {
          return Center(
            child: Padding(
              padding: const EdgeInsets.all(24.0),
              child: Text(
                'الرجاء رفع ملف تكبيرات العيد (mp3/wav) في الملفات أولاً',
                style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
                textAlign: TextAlign.center,
              ),
            ),
          );
        }

        return eidStateAsync.when(
          loading: () => const Center(child: CircularProgressIndicator(color: Colors.white)),
          error: (err, stack) => Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Text('فشل تحميل إعدادات العيد: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
                const SizedBox(height: 12),
                ElevatedButton(
                  onPressed: () => ref.read(eidStateProvider.notifier).refresh(),
                  child: Text('تحديث', style: GoogleFonts.cairo()),
                ),
              ],
            ),
          ),
          data: (eidState) {
            // Initialize local variables once data is loaded
            if (_localEidPrayers == null) {
              _localEidPrayers = eidState.prayers.map((p) => Map<String, dynamic>.from(p)).toList();
            }
            if (_selectedEidTakbeerFile == null) {
              _selectedEidTakbeerFile = _findMatchingFile(audioFiles, eidState.takbeerFile) ?? (audioFiles.isNotEmpty ? audioFiles.first : null);
            }
            if (_localEidTakbeerVolume == null) {
              _localEidTakbeerVolume = eidState.takbeerVolume;
            }

            return SingleChildScrollView(
              padding: const EdgeInsets.all(16.0),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  // Eid General Mode Switch (Manual + Automatic Hijri Calendar)
                  Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.nights_stay, color: Color(0xFFF1C40F)),
                            const SizedBox(width: 10),
                            Expanded(
                              child: Text(
                                'إعدادات تفعيل وضع العيد',
                                style: GoogleFonts.cairo(fontWeight: FontWeight.bold, color: Colors.white, fontSize: 15),
                              ),
                            ),
                            IconButton(
                              icon: const Icon(Icons.refresh, color: Colors.white54, size: 20),
                              tooltip: 'تحديث حالة العيد',
                              onPressed: () => ref.read(eidStateProvider.notifier).refresh(),
                            ),
                          ],
                        ),
                        const Divider(color: Colors.white10, height: 16),
                        SwitchListTile(
                          contentPadding: EdgeInsets.zero,
                          value: eidState.manualEnabled,
                          activeColor: const Color(0xFFF1C40F),
                          title: Text(
                            'تفعيل وضع العيد يدوياً الآن',
                            style: GoogleFonts.cairo(fontWeight: FontWeight.bold, color: Colors.white, fontSize: 13),
                          ),
                          subtitle: Text(
                            'تشغيل تكبيرات وتنبيهات العيد فوراً بغض النظر عن التاريخ الهجري',
                            style: GoogleFonts.cairo(color: Colors.white54, fontSize: 11),
                          ),
                          onChanged: (val) async {
                            final success = await ref.read(eidStateProvider.notifier).toggleMode(val, autoVal: eidState.autoEnabled);
                            if (context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content: Text(success
                                      ? (val ? 'تم تفعيل وضع العيد يدوياً' : 'تم إيقاف التفعيل اليدوي لوضع العيد')
                                      : 'فشل تحديث إعدادات وضع العيد'),
                                  backgroundColor: success ? Colors.green : Colors.redAccent,
                                ),
                              );
                            }
                          },
                        ),
                        const Divider(color: Colors.white10, height: 12),
                        SwitchListTile(
                          contentPadding: EdgeInsets.zero,
                          value: eidState.autoEnabled,
                          activeColor: const Color(0xFF00FFCC),
                          title: Text(
                            'تفعيل أوتوماتيكي عند قدوم أيام العيد',
                            style: GoogleFonts.cairo(fontWeight: FontWeight.bold, color: Colors.white, fontSize: 13),
                          ),
                          subtitle: Text(
                            'يعمل تلقائياً في عيد الفطر (1–3 شوال) ويوم عرفة وعيد الأضحى (9–13 ذو الحجة)',
                            style: GoogleFonts.cairo(color: Colors.white54, fontSize: 11),
                          ),
                          onChanged: (val) async {
                            final success = await ref.read(eidStateProvider.notifier).toggleMode(eidState.manualEnabled, autoVal: val);
                            if (context.mounted) {
                              ScaffoldMessenger.of(context).showSnackBar(
                                SnackBar(
                                  content: Text(success
                                      ? (val ? 'تم تفعيل التشغيل الأوتوماتيكي لأيام العيد' : 'تم إيقاف التشغيل الأوتوماتيكي لأيام العيد')
                                      : 'فشل تحديث إعدادات وضع العيد'),
                                  backgroundColor: success ? Colors.green : Colors.redAccent,
                                ),
                              );
                            }
                          },
                        ),
                        const SizedBox(height: 10),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                          decoration: BoxDecoration(
                            color: eidState.enabled
                                ? const Color(0xFF2ECC71).withOpacity(0.14)
                                : (eidState.autoEnabled
                                    ? const Color(0xFFF1C40F).withOpacity(0.12)
                                    : Colors.white.withOpacity(0.05)),
                            borderRadius: BorderRadius.circular(12),
                            border: Border.all(
                              color: eidState.enabled
                                  ? const Color(0xFF2ECC71).withOpacity(0.4)
                                  : (eidState.autoEnabled
                                      ? const Color(0xFFF1C40F).withOpacity(0.35)
                                      : Colors.white12),
                            ),
                          ),
                          child: Row(
                            children: [
                              Icon(
                                eidState.enabled
                                    ? Icons.check_circle
                                    : (eidState.autoEnabled ? Icons.schedule : Icons.pause_circle_outline),
                                color: eidState.enabled
                                    ? const Color(0xFF2ECC71)
                                    : (eidState.autoEnabled ? const Color(0xFFF1C40F) : Colors.white54),
                                size: 20,
                              ),
                              const SizedBox(width: 10),
                              Expanded(
                                child: Text(
                                  eidState.manualEnabled
                                      ? 'وضع العيد مفعل حالياً (تفعيل يدوي)'
                                      : (eidState.autoEnabled && eidState.autoDayActive)
                                          ? 'وضع العيد مفعل أوتوماتيكياً اليوم: ${eidState.autoReason}'
                                          : eidState.autoEnabled
                                              ? 'التفعيل الأوتوماتيكي جاهز: سيعمل تلقائياً عند دخول عيد الفطر (1–3 شوال) أو عيد الأضحى (9–13 ذو الحجة)'
                                              : 'وضع العيد متوقف (التفعيل اليدوي والأوتوماتيكي غير مفعلين)',
                                  style: GoogleFonts.cairo(
                                    color: eidState.enabled
                                        ? const Color(0xFF2ECC71)
                                        : (eidState.autoEnabled ? const Color(0xFFF1C40F) : Colors.white70),
                                    fontWeight: FontWeight.bold,
                                    fontSize: 12,
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 16),

                  // Takbeer Config Card
                  Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.volume_up, color: Color(0xFF00F2FE), size: 20),
                            const SizedBox(width: 8),
                            Text(
                              'ملف ومستوى صوت التكبيرات',
                              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                            ),
                          ],
                        ),
                        const Divider(color: Colors.white10, height: 24),
                        Text(
                          'ملف التكبيرات',
                          style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                        ),
                        const SizedBox(height: 8),
                        DropdownButtonFormField<String>(
                          dropdownColor: const Color(0xFF1C132E),
                          style: const TextStyle(color: Colors.white, fontSize: 13),
                          value: _findMatchingFile(audioFiles, _selectedEidTakbeerFile),
                          decoration: InputDecoration(
                            filled: true,
                            fillColor: Colors.black12,
                            contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                            border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                          ),
                          items: audioFiles.map((file) {
                            return DropdownMenuItem<String>(
                              value: file,
                              child: Text(file, textDirection: TextDirection.ltr),
                            );
                          }).toList(),
                          onChanged: (val) {
                            setState(() {
                              _selectedEidTakbeerFile = val;
                            });
                          },
                        ),
                        const SizedBox(height: 16),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Text('مستوى الصوت (0-30)', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
                            Text(
                              '${_localEidTakbeerVolume}',
                              style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold),
                            ),
                          ],
                        ),
                        SliderTheme(
                          data: SliderTheme.of(context).copyWith(
                            activeTrackColor: const Color(0xFF00F2FE),
                            inactiveTrackColor: Colors.white.withOpacity(0.1),
                            thumbColor: const Color(0xFF00F2FE),
                            trackHeight: 4,
                            thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                          ),
                          child: Slider(
                            value: _localEidTakbeerVolume!.toDouble(),
                            min: 0,
                            max: 30,
                            divisions: 30,
                            onChanged: (val) {
                              setState(() {
                                _localEidTakbeerVolume = val.toInt();
                              });
                            },
                          ),
                        ),
                        const SizedBox(height: 12),
                        Row(
                          children: [
                            Expanded(
                              child: ElevatedButton.icon(
                                onPressed: () async {
                                  final file = _selectedEidTakbeerFile;
                                  final vol = _localEidTakbeerVolume;
                                  if (file != null && vol != null) {
                                    final prayers = _localEidPrayers ?? [];
                                    final success = await ref.read(eidStateProvider.notifier).saveConfig(
                                      file: file,
                                      volume: vol,
                                      prayers: prayers,
                                    );
                                    if (context.mounted) {
                                      ScaffoldMessenger.of(context).showSnackBar(
                                        SnackBar(
                                          content: Text(success ? 'تم حفظ جدولة التكبيرات بنجاح' : 'فشل حفظ الإعدادات'),
                                          backgroundColor: success ? Colors.green : Colors.redAccent,
                                        ),
                                      );
                                    }
                                  }
                                },
                                icon: const Icon(Icons.save),
                                label: Text('حفظ الجدولة والملف', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: const Color(0xFF00F2FE),
                                  foregroundColor: Colors.white,
                                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                              ),
                            ),
                            const SizedBox(width: 12),
                            ElevatedButton.icon(
                              onPressed: () async {
                                final file = _selectedEidTakbeerFile;
                                final vol = _localEidTakbeerVolume;
                                if (file != null && vol != null) {
                                  final success = await ref.read(eidStateProvider.notifier).triggerTakbeer(file, vol);
                                  if (context.mounted) {
                                    ScaffoldMessenger.of(context).showSnackBar(
                                      SnackBar(
                                        content: Text(success ? 'بدء التشغيل التجريبي' : 'فشل تشغيل التكبيرات'),
                                        backgroundColor: success ? Colors.green : Colors.redAccent,
                                      ),
                                    );
                                  }
                                }
                              },
                              icon: const Icon(Icons.play_arrow),
                              label: Text('تشغيل تجريبي', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: Colors.white12,
                                foregroundColor: Colors.white,
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 16),

                  // Scheduler Card
                  Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.calendar_today, color: Color(0xFF8E44AD), size: 20),
                            const SizedBox(width: 8),
                            Text(
                              'جدولة التكبيرات للصلوات الخمس',
                              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                            ),
                          ],
                        ),
                        const Divider(color: Colors.white10, height: 24),
                        if (_localEidPrayers != null)
                          ListView.separated(
                            shrinkWrap: true,
                            physics: const NeverScrollableScrollPhysics(),
                            itemCount: _localEidPrayers!.length,
                            separatorBuilder: (context, index) => const Divider(color: Colors.white10, height: 16),
                            itemBuilder: (context, index) {
                              final prayer = _localEidPrayers![index];
                              final prayerName = _prayerNames[index];
                              final isEnabled = prayer['enabled'] == true;
                              final beforeVal = prayer['before'] ?? 15;
                              final afterVal = prayer['after'] ?? 15;
                              final isBeforeEnabled = beforeVal > 0;
                              final isAfterEnabled = afterVal > 0;

                              return Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Row(
                                    children: [
                                      Checkbox(
                                        value: isEnabled,
                                        activeColor: const Color(0xFF8E44AD),
                                        onChanged: (val) {
                                          setState(() {
                                            prayer['enabled'] = val;
                                          });
                                        },
                                      ),
                                      Text(
                                        prayerName,
                                        style: GoogleFonts.cairo(
                                          color: isEnabled ? Colors.white : Colors.white30,
                                          fontWeight: FontWeight.bold,
                                          fontSize: 14,
                                        ),
                                      ),
                                    ],
                                  ),
                                  if (isEnabled)
                                    Padding(
                                      padding: const EdgeInsets.only(right: 16.0),
                                      child: Column(
                                        children: [
                                          // Before
                                          Row(
                                            children: [
                                              Checkbox(
                                                value: isBeforeEnabled,
                                                activeColor: const Color(0xFF00F2FE),
                                                onChanged: (val) {
                                                  setState(() {
                                                    prayer['before'] = val == true ? 15 : 0;
                                                  });
                                                },
                                              ),
                                              Text('قبل الصلاة بـ', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                                              const SizedBox(width: 8),
                                              if (isBeforeEnabled) ...[
                                                IconButton(
                                                  icon: const Icon(Icons.remove_circle_outline, color: Colors.white60, size: 18),
                                                  onPressed: () {
                                                    setState(() {
                                                      prayer['before'] = (beforeVal - 5).clamp(5, 120);
                                                    });
                                                  },
                                                ),
                                                Text(
                                                  '$beforeVal',
                                                  style: GoogleFonts.orbitron(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                                                ),
                                                IconButton(
                                                  icon: const Icon(Icons.add_circle_outline, color: Color(0xFF00F2FE), size: 18),
                                                  onPressed: () {
                                                    setState(() {
                                                      prayer['before'] = (beforeVal + 5).clamp(5, 120);
                                                    });
                                                  },
                                                ),
                                                Text('دقائق', style: GoogleFonts.cairo(color: Colors.white54, fontSize: 11)),
                                              ],
                                            ],
                                          ),
                                          // After
                                          Row(
                                            children: [
                                              Checkbox(
                                                value: isAfterEnabled,
                                                activeColor: const Color(0xFF00F2FE),
                                                onChanged: (val) {
                                                  setState(() {
                                                    prayer['after'] = val == true ? 15 : 0;
                                                  });
                                                },
                                              ),
                                              Text('بعد الصلاة بـ', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                                              const SizedBox(width: 8),
                                              if (isAfterEnabled) ...[
                                                IconButton(
                                                  icon: const Icon(Icons.remove_circle_outline, color: Colors.white60, size: 18),
                                                  onPressed: () {
                                                    setState(() {
                                                      prayer['after'] = (afterVal - 5).clamp(5, 120);
                                                    });
                                                  },
                                                ),
                                                Text(
                                                  '$afterVal',
                                                  style: GoogleFonts.orbitron(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                                                ),
                                                IconButton(
                                                  icon: const Icon(Icons.add_circle_outline, color: Color(0xFF00F2FE), size: 18),
                                                  onPressed: () {
                                                    setState(() {
                                                      prayer['after'] = (afterVal + 5).clamp(5, 120);
                                                    });
                                                  },
                                                ),
                                                Text('دقائق', style: GoogleFonts.cairo(color: Colors.white54, fontSize: 11)),
                                              ],
                                            ],
                                          ),
                                        ],
                                      ),
                                    ),
                                ],
                              );
                            },
                          ),
                      ],
                    ),
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }

  Widget _buildMaghribTab() {
    final filesState = ref.watch(filesProvider);
    final maghribStateAsync = ref.watch(maghribAlertsProvider);

    return filesState.when(
      loading: () => const Center(child: CircularProgressIndicator(color: Colors.white)),
      error: (err, stack) => Center(child: Text('فشل تحميل الملفات: $err', style: GoogleFonts.cairo(color: Colors.white))),
      data: (filesData) {
        final List filesList = filesData['files'] as List? ?? [];
        final List<String> audioFiles = [];
        for (var f in filesList) {
          final String name = f['name'] ?? '';
          final bool isDir = f['isDirectory'] ?? false;
          if (!isDir && !name.startsWith('prayer_csv/') && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
            audioFiles.add(name);
          }
        }

        if (audioFiles.isEmpty) {
          return Center(
            child: Padding(
              padding: const EdgeInsets.all(24.0),
              child: Text(
                'الرجاء رفع ملفات صوتية للتنبيهات أولاً',
                style: GoogleFonts.cairo(color: const Color(0xFFADCCFF)),
                textAlign: TextAlign.center,
              ),
            ),
          );
        }

        return maghribStateAsync.when(
          loading: () => const Center(child: CircularProgressIndicator(color: Colors.white)),
          error: (err, stack) => Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Text('فشل تحميل إعدادات المغرب: $err', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
                const SizedBox(height: 12),
                ElevatedButton(
                  onPressed: () => ref.read(maghribAlertsProvider.notifier).refresh(),
                  child: Text('تحديث', style: GoogleFonts.cairo()),
                ),
              ],
            ),
          ),
          data: (maghribState) {
            // Initialize local variables
            if (_localMaghribOffset == null) {
              _localMaghribOffset = maghribState.offset;
            }
            if (_localMaghribAlerts == null) {
              _localMaghribAlerts = maghribState.alerts.map((a) => Map<String, dynamic>.from(a)).toList();
            }

            return SingleChildScrollView(
              padding: const EdgeInsets.all(16.0),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  // Offset Card
                  Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'توقيت تنبيه المغرب قبل الأذان',
                          style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                        ),
                        const Divider(color: Colors.white10, height: 24),
                        Row(
                          children: [
                            Text(
                              'قبل المغرب بـ',
                              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13),
                            ),
                            const SizedBox(width: 8),
                            IconButton(
                              constraints: const BoxConstraints(),
                              padding: const EdgeInsets.all(8),
                              icon: const Icon(Icons.remove_circle_outline, color: Colors.white60, size: 20),
                              onPressed: () {
                                if (_localMaghribOffset! > 0) {
                                  setState(() {
                                    _localMaghribOffset = _localMaghribOffset! - 1;
                                  });
                                }
                              },
                            ),
                            Text(
                              '$_localMaghribOffset',
                              style: GoogleFonts.orbitron(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
                            ),
                            IconButton(
                              constraints: const BoxConstraints(),
                              padding: const EdgeInsets.all(8),
                              icon: const Icon(Icons.add_circle_outline, color: Color(0xFF00F2FE), size: 20),
                              onPressed: () {
                                setState(() {
                                  _localMaghribOffset = _localMaghribOffset! + 1;
                                });
                              },
                            ),
                            const SizedBox(width: 4),
                            Text(
                              'دقائق',
                              style: GoogleFonts.cairo(color: Colors.white54, fontSize: 12),
                            ),
                          ],
                        ),
                        const SizedBox(height: 12),
                        SizedBox(
                          width: double.infinity,
                          child: ElevatedButton.icon(
                            onPressed: () async {
                              final success = await ref.read(maghribAlertsProvider.notifier).saveOffset(_localMaghribOffset!);
                              if (context.mounted) {
                                ScaffoldMessenger.of(context).showSnackBar(
                                  SnackBar(
                                    content: Text(success ? 'تم حفظ الإزاحة بنجاح' : 'فشل حفظ الإزاحة'),
                                    backgroundColor: success ? Colors.green : Colors.redAccent,
                                  ),
                                );
                              }
                            },
                            icon: const Icon(Icons.save, size: 16),
                            label: Text('حفظ الإزاحة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                            style: ElevatedButton.styleFrom(
                              backgroundColor: const Color(0xFF00F2FE),
                              foregroundColor: Colors.white,
                              padding: const EdgeInsets.symmetric(vertical: 10),
                              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 16),

                  // Weekly Alerts List Card
                  Container(
                    padding: const EdgeInsets.all(16),
                    decoration: BoxDecoration(
                      color: const Color(0x7316132C),
                      borderRadius: BorderRadius.circular(20),
                      border: Border.all(color: Colors.white.withOpacity(0.08)),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Row(
                          children: [
                            const Icon(Icons.calendar_view_week, color: Color(0xFF8E44AD), size: 20),
                            const SizedBox(width: 8),
                            Text(
                              'إعدادات التنبيهات لأيام الأسبوع',
                              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                            ),
                          ],
                        ),
                        const Divider(color: Colors.white10, height: 24),
                        if (_localMaghribAlerts != null)
                          ListView.separated(
                            shrinkWrap: true,
                            physics: const NeverScrollableScrollPhysics(),
                            itemCount: _localMaghribAlerts!.length,
                            separatorBuilder: (context, index) => const Divider(color: Colors.white10, height: 24),
                            itemBuilder: (context, index) {
                              final alert = _localMaghribAlerts![index];
                              final dayName = _daysOfWeek[index];
                              final isEnabled = alert['enabled'] == true;
                              final file = alert['file'] as String? ?? '';
                              final volume = alert['volume'] ?? 15;

                              // Ensure a valid selected file
                              final resolvedFile = _findMatchingFile(audioFiles, file);
                              if (resolvedFile != null) {
                                alert['file'] = resolvedFile;
                              } else if (audioFiles.isNotEmpty) {
                                alert['file'] = audioFiles.first;
                              } else {
                                alert['file'] = '';
                              }

                              return Column(
                                crossAxisAlignment: CrossAxisAlignment.stretch,
                                children: [
                                  Row(
                                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                                    children: [
                                      Row(
                                        children: [
                                          const Icon(Icons.today, color: Color(0xFFF1C40F), size: 18),
                                          const SizedBox(width: 8),
                                          Text(
                                            dayName,
                                            style: GoogleFonts.cairo(
                                              color: Colors.white,
                                              fontWeight: FontWeight.bold,
                                              fontSize: 14,
                                            ),
                                          ),
                                        ],
                                      ),
                                      Switch(
                                        value: isEnabled,
                                        activeColor: const Color(0xFF8E44AD),
                                        onChanged: (val) {
                                          setState(() {
                                            alert['enabled'] = val;
                                          });
                                        },
                                      ),
                                    ],
                                  ),
                                  if (isEnabled) ...[
                                    const SizedBox(height: 8),
                                    Text('ملف التنبيه', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                                    const SizedBox(height: 6),
                                    DropdownButtonFormField<String>(
                                      isExpanded: true,
                                      dropdownColor: const Color(0xFF1C132E),
                                      style: const TextStyle(color: Colors.white, fontSize: 13),
                                      value: _findMatchingFile(audioFiles, alert['file'] as String?),
                                      decoration: InputDecoration(
                                        filled: true,
                                        fillColor: Colors.black12,
                                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                                      ),
                                      items: audioFiles.map((f) {
                                        return DropdownMenuItem<String>(
                                          value: f,
                                          child: Text(f, textDirection: TextDirection.ltr),
                                        );
                                      }).toList(),
                                      onChanged: (val) {
                                        setState(() {
                                          alert['file'] = val;
                                        });
                                      },
                                    ),
                                    const SizedBox(height: 12),
                                    Row(
                                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                                      children: [
                                        Text('مستوى الصوت', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                                        Text(
                                          '$volume',
                                          style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold, fontSize: 12),
                                        ),
                                      ],
                                    ),
                                    SliderTheme(
                                      data: SliderTheme.of(context).copyWith(
                                        activeTrackColor: const Color(0xFF00F2FE),
                                        inactiveTrackColor: Colors.white.withOpacity(0.1),
                                        thumbColor: const Color(0xFF00F2FE),
                                        trackHeight: 4,
                                        thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                                      ),
                                      child: Slider(
                                        value: volume.toDouble(),
                                        min: 0,
                                        max: 30,
                                        divisions: 30,
                                        onChanged: (val) {
                                          setState(() {
                                            alert['volume'] = val.toInt();
                                          });
                                        },
                                      ),
                                    ),
                                  ],
                                ],
                              );
                            },
                          ),
                        const SizedBox(height: 24),
                        Row(
                          children: [
                            Expanded(
                              child: ElevatedButton.icon(
                                onPressed: () async {
                                  final success = await ref.read(maghribAlertsProvider.notifier).saveAlerts(_localMaghribAlerts!);
                                  if (context.mounted) {
                                    ScaffoldMessenger.of(context).showSnackBar(
                                      SnackBar(
                                        content: Text(success ? 'تم حفظ تنبيهات المغرب بنجاح' : 'فشل حفظ التنبيهات'),
                                        backgroundColor: success ? Colors.green : Colors.redAccent,
                                      ),
                                    );
                                  }
                                },
                                icon: const Icon(Icons.save),
                                label: Text('حفظ كل تنبيهات الأسبوع', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                                style: ElevatedButton.styleFrom(
                                  backgroundColor: const Color(0xFF8E44AD),
                                  foregroundColor: Colors.white,
                                  padding: const EdgeInsets.symmetric(vertical: 12),
                                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                                ),
                              ),
                            ),
                            const SizedBox(width: 12),
                            ElevatedButton.icon(
                              onPressed: () {
                                ref.read(maghribAlertsProvider.notifier).refresh();
                                setState(() {
                                  _localMaghribAlerts = null;
                                  _localMaghribOffset = null;
                                });
                              },
                              icon: const Icon(Icons.refresh),
                              label: Text('تحديث', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: Colors.white12,
                                foregroundColor: Colors.white,
                                padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
                                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }
}

// --- Sub-widgets for settings ---

class _AdhanSettingsCard extends ConsumerStatefulWidget {
  const _AdhanSettingsCard();

  @override
  ConsumerState<_AdhanSettingsCard> createState() => _AdhanSettingsCardState();
}

class _AdhanSettingsCardState extends ConsumerState<_AdhanSettingsCard> {
  String? _fajrFile;
  String? _otherFile;
  String? _iqamaFile;
  final List<bool> _iqamaEnabled = List.filled(5, false);
  final List<TextEditingController> _iqamaDelays = List.generate(5, (_) => TextEditingController(text: '10'));
  bool _initialized = false;
  bool _isSaving = false;

  @override
  Widget build(BuildContext context) {
    final adhanState = ref.watch(adhanConfigProvider);
    final filesState = ref.watch(filesProvider);

    final List<String> audioFiles = [];
    filesState.whenData((data) {
      final list = data['files'] as List? ?? [];
      for (var f in list) {
        final name = f['name'] as String? ?? '';
        final isDir = f['isDirectory'] as bool? ?? false;
        if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
          audioFiles.add(name);
        }
      }
    });

    return adhanState.when(
      loading: () => const Center(child: Padding(
        padding: EdgeInsets.all(24.0),
        child: CircularProgressIndicator(color: Color(0xFF00F2FE)),
      )),
      error: (err, stack) => Column(
        children: [
          Text('خطأ في تحميل إعدادات الأذان: $err', style: const TextStyle(color: Colors.redAccent)),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: () => ref.read(adhanConfigProvider.notifier).refresh(),
            child: const Text('إعادة المحاولة'),
          )
        ],
      ),
      data: (config) {
        if (!_initialized) {
          _fajrFile = config['fajr'] as String?;
          _otherFile = config['adhan'] as String?;
          _iqamaFile = config['iqama'] as String?;
          
          for (int i = 0; i < 5; i++) {
            _iqamaEnabled[i] = (config['iqama_en_$i'] == 1 || config['iqama_en_$i'] == '1' || config['iqama_en_$i'] == true);
            _iqamaDelays[i].text = (config['iqama_del_$i'] ?? '10').toString();
          }
          _initialized = true;
        }

        return Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: const Color(0x7316132C),
            borderRadius: BorderRadius.circular(24),
            border: Border.all(color: Colors.white.withOpacity(0.08)),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withOpacity(0.05),
                blurRadius: 15,
                offset: const Offset(0, 8),
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Row(
                children: [
                  const Icon(Icons.mosque_outlined, color: Color(0xFF00F2FE), size: 22),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Text(
                      'ضبط ملفات الأذان والإقامة',
                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 20),

              DropdownButtonFormField<String>(
                isExpanded: true,
                dropdownColor: const Color(0xFF1C132E),
                value: _findMatchingFile(audioFiles, _fajrFile),
                style: const TextStyle(color: Colors.white),
                decoration: InputDecoration(
                  labelText: 'أذان الفجر',
                  labelStyle: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
                  filled: true,
                  fillColor: Colors.white.withOpacity(0.04),
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  enabledBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  focusedBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                  ),
                ),
                items: audioFiles.map((f) => DropdownMenuItem(value: f, child: Text(f, style: GoogleFonts.cairo(color: Colors.white, fontSize: 13), overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                onChanged: (val) {
                  setState(() {
                    _fajrFile = val;
                  });
                },
              ),
              const SizedBox(height: 12),

              DropdownButtonFormField<String>(
                isExpanded: true,
                dropdownColor: const Color(0xFF1C132E),
                value: _findMatchingFile(audioFiles, _otherFile),
                style: const TextStyle(color: Colors.white),
                decoration: InputDecoration(
                  labelText: 'باقي الصلوات',
                  labelStyle: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
                  filled: true,
                  fillColor: Colors.white.withOpacity(0.04),
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  enabledBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  focusedBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                  ),
                ),
                items: audioFiles.map((f) => DropdownMenuItem(value: f, child: Text(f, style: GoogleFonts.cairo(color: Colors.white, fontSize: 13), overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                onChanged: (val) {
                  setState(() {
                    _otherFile = val;
                  });
                },
              ),
              const SizedBox(height: 12),

              DropdownButtonFormField<String>(
                isExpanded: true,
                dropdownColor: const Color(0xFF1C132E),
                value: _findMatchingFile(audioFiles, _iqamaFile),
                style: const TextStyle(color: Colors.white),
                decoration: InputDecoration(
                  labelText: 'ملف الإقامة',
                  labelStyle: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
                  filled: true,
                  fillColor: Colors.white.withOpacity(0.04),
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  enabledBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                  ),
                  focusedBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(16),
                    borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                  ),
                ),
                items: audioFiles.map((f) => DropdownMenuItem(value: f, child: Text(f, style: GoogleFonts.cairo(color: Colors.white, fontSize: 13), overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                onChanged: (val) {
                  setState(() {
                    _iqamaFile = val;
                  });
                },
              ),
              const SizedBox(height: 28),

              Text(
                'ضبط أوقات وتفعيل الإقامة للصلوات',
                style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.w900, fontSize: 14),
              ),
              const SizedBox(height: 12),

              ...List.generate(5, (index) {
                final prayerName = ['الفجر', 'الظهر', 'العصر', 'المغرب', 'العشاء'][index];
                final isEnabled = _iqamaEnabled[index];
                return Container(
                  margin: const EdgeInsets.only(bottom: 10),
                  padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                  decoration: BoxDecoration(
                    color: const Color(0x7316132C),
                    borderRadius: BorderRadius.circular(16),
                    border: Border.all(color: Colors.white.withOpacity(0.08)),
                  ),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      // Prayer Name & Toggle Column
                      Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            prayerName,
                            style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
                          ),
                          const SizedBox(height: 4),
                          Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Text('تفعيل الإقامة', style: GoogleFonts.cairo(color: Colors.white60, fontSize: 11)),
                              const SizedBox(width: 4),
                              SizedBox(
                                height: 32,
                                child: Switch(
                                  value: isEnabled,
                                  activeColor: const Color(0xFF00F2FE),
                                  onChanged: (val) {
                                    setState(() {
                                      _iqamaEnabled[index] = val;
                                    });
                                  },
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                      // Delay text input on the right (shown only if enabled)
                      if (isEnabled)
                        SizedBox(
                          width: 110,
                          child: TextField(
                            controller: _iqamaDelays[index],
                            style: const TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.bold),
                            keyboardType: TextInputType.number,
                            decoration: InputDecoration(
                              labelText: 'الانتظار (دقيقة)',
                              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 11),
                              filled: true,
                              fillColor: Colors.white.withOpacity(0.02),
                              border: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(12),
                                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                              ),
                              enabledBorder: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(12),
                                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                              ),
                              focusedBorder: OutlineInputBorder(
                                borderRadius: BorderRadius.circular(12),
                                borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                              ),
                              contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 10),
                            ),
                          ),
                        ),
                    ],
                  ),
                );
              }),

              const SizedBox(height: 20),
              ElevatedButton.icon(
                onPressed: _isSaving ? null : () async {
                  setState(() {
                    _isSaving = true;
                  });

                  final payload = <String, String>{};
                  if (_fajrFile != null) payload['fajr'] = _fajrFile!;
                  if (_otherFile != null) payload['adhan'] = _otherFile!;
                  if (_iqamaFile != null) payload['iqama'] = _iqamaFile!;
                  
                  for (int i = 0; i < 5; i++) {
                    payload['iqama_en_$i'] = _iqamaEnabled[i] ? '1' : '0';
                    payload['iqama_del_$i'] = _iqamaDelays[i].text.trim().isEmpty 
                        ? '10' 
                        : _iqamaDelays[i].text.trim();
                  }

                  final success = await ref.read(adhanConfigProvider.notifier).save(payload);
                  
                  setState(() {
                    _isSaving = false;
                  });

                  if (context.mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(success ? 'تم حفظ إعدادات الأذان بنجاح' : 'فشل حفظ الإعدادات'),
                        backgroundColor: success ? Colors.green : Colors.redAccent,
                      ),
                    );
                  }
                },
                icon: _isSaving 
                    ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                    : const Icon(Icons.save_outlined),
                label: Text('حفظ إعدادات الأذان والإقامة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                style: ElevatedButton.styleFrom(
                  backgroundColor: const Color(0xFF00F2FE),
                  foregroundColor: Colors.white,
                  padding: const EdgeInsets.symmetric(vertical: 14),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
                  side: BorderSide(color: Colors.white.withOpacity(0.08)),
                  elevation: 8,
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}


// --- Presence & Smart Welcome Settings Card ---

class _PresenceSettingsCard extends ConsumerStatefulWidget {
  const _PresenceSettingsCard();

  @override
  ConsumerState<_PresenceSettingsCard> createState() => _PresenceSettingsCardState();
}

class _PresenceSettingsCardState extends ConsumerState<_PresenceSettingsCard> {
  bool _initialized = false;
  bool _enabled = false;
  int _mode = 0; // 0=Hybrid, 1=BLE, 2=WiFi
  final TextEditingController _targetNameController = TextEditingController();
  final TextEditingController _targetMacController = TextEditingController();
  int _rssiThreshold = -70;
  String? _welcomeFile;
  int _volume = 20;
  final TextEditingController _cooldownController = TextEditingController(text: '30');
  final TextEditingController _vacateController = TextEditingController(text: '3');
  bool _quietEnabled = false;
  TimeOfDay _quietStart = const TimeOfDay(hour: 23, minute: 0);
  TimeOfDay _quietEnd = const TimeOfDay(hour: 7, minute: 0);

  Timer? _statusTimer;

  @override
  void initState() {
    super.initState();
    // Poll status periodically while on this screen
    _statusTimer = Timer.periodic(const Duration(seconds: 4), (_) {
      if (mounted) {
        ref.read(presenceProvider.notifier).refreshStatusOnly();
      }
    });
  }

  @override
  void dispose() {
    _statusTimer?.cancel();
    _targetNameController.dispose();
    _targetMacController.dispose();
    _cooldownController.dispose();
    _vacateController.dispose();
    super.dispose();
  }

  void _syncWithConfig(Map<String, dynamic> config) {
    if (_initialized) return;
    if (config.isEmpty) return;
    _enabled = config['enabled'] == true || config['enabled'] == 1 || config['enabled'] == 'true';
    _mode = (config['mode'] as num?)?.toInt() ?? 0;
    _targetNameController.text = (config['targetName'] ?? config['bleName'] ?? '').toString();
    _targetMacController.text = (config['targetMac'] ?? config['bleMac'] ?? '').toString();
    _rssiThreshold = (config['rssiThresh'] as num?)?.toInt() ?? -70;
    _welcomeFile = (config['welcomeFile'] ?? '/Azan/welcome.mp3').toString();
    _volume = (config['volume'] as num?)?.toInt() ?? 20;
    _cooldownController.text = ((config['cooldownMinutes'] as num?)?.toInt() ?? 30).toString();
    _vacateController.text = ((config['vacateMinutes'] as num?)?.toInt() ?? 3).toString();
    _quietEnabled = config['quietEn'] == true || config['quietEn'] == 1;
    final qsh = (config['qStartH'] as num?)?.toInt() ?? 23;
    final qsm = (config['qStartM'] as num?)?.toInt() ?? 0;
    final qeh = (config['qEndH'] as num?)?.toInt() ?? 7;
    final qem = (config['qEndM'] as num?)?.toInt() ?? 0;
    _quietStart = TimeOfDay(hour: qsh, minute: qsm);
    _quietEnd = TimeOfDay(hour: qeh, minute: qem);
    _initialized = true;
  }

  Future<void> _handleSave() async {
    final cfg = {
      'enabled': _enabled ? 1 : 0,
      'mode': _mode,
      'targetMac': _targetMacController.text.trim(),
      'targetName': _targetNameController.text.trim(),
      'rssiThresh': _rssiThreshold,
      'welcomeFile': _welcomeFile ?? '/Azan/welcome.mp3',
      'volume': _volume,
      'cooldownMinutes': int.tryParse(_cooldownController.text.trim()) ?? 30,
      'vacateMinutes': int.tryParse(_vacateController.text.trim()) ?? 3,
      'quietEn': _quietEnabled ? 1 : 0,
      'qStartH': _quietStart.hour,
      'qStartM': _quietStart.minute,
      'qEndH': _quietEnd.hour,
      'qEndM': _quietEnd.minute,
    };
    final ok = await ref.read(presenceProvider.notifier).save(cfg);
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(ok ? 'تم حفظ إعدادات كشف الحضور بنجاح' : 'فشل حفظ الإعدادات'),
          backgroundColor: ok ? Colors.green : Colors.redAccent,
        ),
      );
    }
  }

  Future<void> _showBleScanDialog() async {
    ref.read(presenceProvider.notifier).scanBle();
    showDialog(
      context: context,
      builder: (dialogCtx) {
        return Consumer(
          builder: (context, ref, _) {
            final pState = ref.watch(presenceProvider);
            return Directionality(
              textDirection: TextDirection.rtl,
              child: AlertDialog(
                backgroundColor: const Color(0xFF1C132E),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
                title: Row(
                  children: [
                    const Icon(Icons.bluetooth_searching, color: Color(0xFF00F2FE)),
                    const SizedBox(width: 8),
                    Text('الأجهزة المكتشفة بالبلوتوث', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16)),
                  ],
                ),
                content: SizedBox(
                  width: double.maxFinite,
                  child: pState.isBleScanning
                      ? Padding(
                          padding: const EdgeInsets.all(24.0),
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const CircularProgressIndicator(color: Color(0xFF00F2FE)),
                              const SizedBox(height: 16),
                              Text('جاري فحص الأجهزة المحيطة (BLE)...', style: GoogleFonts.cairo(color: Colors.white70)),
                            ],
                          ),
                        )
                      : pState.bleDevices.isEmpty
                          ? Padding(
                              padding: const EdgeInsets.all(16.0),
                              child: Text('لم يتم العثور على أجهزة بلوتوث قريبة. تأكد من تفعيل البلوتوث بهاتفك.',
                                  style: GoogleFonts.cairo(color: Colors.white60)),
                            )
                          : ListView.separated(
                              shrinkWrap: true,
                              itemCount: pState.bleDevices.length,
                              separatorBuilder: (_, __) => const Divider(color: Colors.white12),
                              itemBuilder: (context, index) {
                                final d = pState.bleDevices[index];
                                final name = (d['name'] ?? 'جهاز مجهول').toString();
                                final mac = (d['mac'] ?? '').toString();
                                final rssi = d['rssi'] ?? -100;
                                return ListTile(
                                  leading: const Icon(Icons.devices_other, color: Color(0xFF00F2FE)),
                                  title: Text(name.isEmpty ? 'جهاز مجهول' : name,
                                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
                                  subtitle: Text('$mac  (إشارة: $rssi dBm)',
                                      style: const TextStyle(color: Colors.white54, fontSize: 12)),
                                  onTap: () {
                                    setState(() {
                                      _targetMacController.text = mac;
                                      if (name.isNotEmpty && name != 'جهاز مجهول') {
                                        _targetNameController.text = name;
                                      }
                                    });
                                    Navigator.pop(dialogCtx);
                                  },
                                );
                              },
                            ),
                ),
                actions: [
                  TextButton(
                    onPressed: () => ref.read(presenceProvider.notifier).scanBle(),
                    child: Text('إعادة الفحص', style: GoogleFonts.cairo(color: const Color(0xFF00F2FE))),
                  ),
                  TextButton(
                    onPressed: () => Navigator.pop(dialogCtx),
                    child: Text('إغلاق', style: GoogleFonts.cairo(color: Colors.grey)),
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }

  Future<void> _showWifiScanDialog() async {
    ref.read(presenceProvider.notifier).scanWifi();
    showDialog(
      context: context,
      builder: (dialogCtx) {
        return Consumer(
          builder: (context, ref, _) {
            final pState = ref.watch(presenceProvider);
            return Directionality(
              textDirection: TextDirection.rtl,
              child: AlertDialog(
                backgroundColor: const Color(0xFF1C132E),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
                title: Row(
                  children: [
                    const Icon(Icons.wifi_find, color: Color(0xFF00F2FE)),
                    const SizedBox(width: 8),
                    Text('أجهزة شبكة الـ Wi-Fi', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16)),
                  ],
                ),
                content: SizedBox(
                  width: double.maxFinite,
                  child: pState.isWifiScanning
                      ? Padding(
                          padding: const EdgeInsets.all(24.0),
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const CircularProgressIndicator(color: Color(0xFF00F2FE)),
                              const SizedBox(height: 16),
                              Text('جاري فحص الأجهزة المتصلة بالراوتر...', style: GoogleFonts.cairo(color: Colors.white70)),
                            ],
                          ),
                        )
                      : pState.wifiDevices.isEmpty
                          ? Padding(
                              padding: const EdgeInsets.all(16.0),
                              child: Text('لم يتم العثور على أجهزة في جدول الشبكة حالياً.',
                                  style: GoogleFonts.cairo(color: Colors.white60)),
                            )
                          : ListView.separated(
                              shrinkWrap: true,
                              itemCount: pState.wifiDevices.length,
                              separatorBuilder: (_, __) => const Divider(color: Colors.white12),
                              itemBuilder: (context, index) {
                                final d = pState.wifiDevices[index];
                                final ip = (d['ip'] ?? '').toString();
                                final mac = (d['mac'] ?? '').toString();
                                return ListTile(
                                  leading: const Icon(Icons.phone_android, color: Color(0xFF00F2FE)),
                                  title: Text('IP: $ip',
                                      style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
                                  subtitle: Text('MAC: $mac',
                                      style: const TextStyle(color: Colors.white54, fontSize: 12)),
                                  onTap: () {
                                    setState(() {
                                      _targetMacController.text = mac;
                                    });
                                    Navigator.pop(dialogCtx);
                                  },
                                );
                              },
                            ),
                ),
                actions: [
                  TextButton(
                    onPressed: () => ref.read(presenceProvider.notifier).scanWifi(),
                    child: Text('إعادة الفحص', style: GoogleFonts.cairo(color: const Color(0xFF00F2FE))),
                  ),
                  TextButton(
                    onPressed: () => Navigator.pop(dialogCtx),
                    child: Text('إغلاق', style: GoogleFonts.cairo(color: Colors.grey)),
                  ),
                ],
              ),
            );
          },
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    final pState = ref.watch(presenceProvider);
    final filesState = ref.watch(filesProvider);

    if (pState.config.isNotEmpty && !_initialized) {
      _syncWithConfig(pState.config);
    }

    final List<String> audioFiles = [];
    filesState.whenData((data) {
      final list = data['files'] as List? ?? [];
      for (var f in list) {
        final name = f['name'] as String? ?? '';
        final isDir = f['isDirectory'] as bool? ?? false;
        if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
          audioFiles.add(name);
        }
      }
    });

    final status = pState.status;
    final stateStr = (status['state'] ?? 'away').toString();
    final lastRssi = status['lastRssi'] ?? -100;
    final lastSeenMs = (status['lastSeenMs'] as num?)?.toInt() ?? 0;

    String stateTitle = 'غائب (خارج النطاق)';
    Color stateColor = Colors.grey;
    IconData stateIcon = Icons.location_off_outlined;

    if (stateStr == 'present') {
      stateTitle = 'متواجد في المكان';
      stateColor = Colors.greenAccent;
      stateIcon = Icons.person_pin_circle;
    } else if (stateStr == 'cooldown') {
      stateTitle = 'فترة الانتظار (Cooldown)';
      stateColor = const Color(0xFF00F2FE);
      stateIcon = Icons.hourglass_top_rounded;
    }

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.doorbell_outlined, color: Color(0xFF00F2FE), size: 24),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'كشف الحضور والترحيب الذكي',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            'تشغيل ملف صوتي ترحيبي تلقائياً عند عودتك للمنزل (عبر بلوتوث الهاتف أو شبكة الـ Wi-Fi)',
            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
          ),
          const Divider(color: Colors.white12, height: 28),

          // Live Status Box
          Container(
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: Colors.black26,
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: stateColor.withOpacity(0.3)),
            ),
            child: Row(
              children: [
                Icon(stateIcon, color: stateColor, size: 28),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Text('الحالة الحالية: ', style: GoogleFonts.cairo(color: Colors.white70, fontSize: 12)),
                          Text(stateTitle, style: GoogleFonts.cairo(color: stateColor, fontWeight: FontWeight.bold, fontSize: 13)),
                        ],
                      ),
                      const SizedBox(height: 2),
                      Text(
                        'قوة الإشارة: $lastRssi dBm  |  آخر رصد: ${(lastSeenMs / 1000).toStringAsFixed(0)} ثانية',
                        style: const TextStyle(color: Colors.white38, fontSize: 11),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),

          // Master Enable Toggle
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: Text('تفعيل نظام الترحيب والحضور', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
            subtitle: Text('تشغيل الصوت الترحيبي عند رصد جهازك بعد غياب', style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12)),
            value: _enabled,
            activeColor: const Color(0xFF00F2FE),
            onChanged: (val) {
              setState(() {
                _enabled = val;
              });
            },
          ),
          const SizedBox(height: 12),

          // Mode Selection
          Text('طريقة الرصد (Mode):', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          SegmentedButton<int>(
            segments: const [
              ButtonSegment(value: 0, label: Text('هجين BLE+WiFi')),
              ButtonSegment(value: 1, label: Text('بلوتوث BLE')),
              ButtonSegment(value: 2, label: Text('واي فاي WiFi')),
            ],
            selected: {_mode},
            onSelectionChanged: (set) {
              setState(() {
                _mode = set.first;
              });
            },
            style: ButtonStyle(
              backgroundColor: MaterialStateProperty.resolveWith((states) {
                if (states.contains(MaterialState.selected)) return const Color(0xFF00F2FE).withOpacity(0.25);
                return Colors.black26;
              }),
              foregroundColor: MaterialStateProperty.resolveWith((states) {
                if (states.contains(MaterialState.selected)) return const Color(0xFF00F2FE);
                return Colors.white70;
              }),
            ),
          ),
          const SizedBox(height: 20),

          // Target Device Fields
          Text('الجهاز المستهدف (هاتفك أو ساعتك):', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          TextField(
            controller: _targetNameController,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              labelText: 'اسم الجهاز (اختياري)',
              labelStyle: GoogleFonts.cairo(color: Colors.white60),
              prefixIcon: const Icon(Icons.devices, color: Color(0xFF00F2FE)),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
            ),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _targetMacController,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              labelText: 'عنوان MAC المستهدف (مثال: AA:BB:CC:DD:EE:FF)',
              labelStyle: GoogleFonts.cairo(color: Colors.white60),
              prefixIcon: const Icon(Icons.fingerprint, color: Color(0xFF00F2FE)),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
            ),
          ),
          const SizedBox(height: 10),

          // Scan buttons
          Row(
            children: [
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: _showBleScanDialog,
                  icon: const Icon(Icons.bluetooth_searching, size: 18),
                  label: Text('بحث أجهزة BLE', style: GoogleFonts.cairo(fontSize: 12)),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: const Color(0xFF00F2FE),
                    side: const BorderSide(color: Color(0xFF00F2FE)),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                child: OutlinedButton.icon(
                  onPressed: _showWifiScanDialog,
                  icon: const Icon(Icons.wifi_find, size: 18),
                  label: Text('فحص أجهزة الـ WiFi', style: GoogleFonts.cairo(fontSize: 12)),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: const Color(0xFF00FFCC),
                    side: const BorderSide(color: Color(0xFF00FFCC)),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          // Proximity RSSI Slider (for BLE / Hybrid)
          if (_mode != 2) ...[
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text('حساسية الاقتراب (RSSI Threshold):', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
                Text('$_rssiThreshold dBm', style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold)),
              ],
            ),
            Slider(
              value: _rssiThreshold.toDouble().clamp(-90.0, -40.0),
              min: -90,
              max: -40,
              divisions: 50,
              activeColor: const Color(0xFF00F2FE),
              onChanged: (val) {
                setState(() {
                  _rssiThreshold = val.round();
                });
              },
            ),
            const SizedBox(height: 12),
          ],

          // Welcome Sound File
          Text('ملف الترحيب الصوتي:', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
          const SizedBox(height: 8),
          DropdownButtonFormField<String>(
            isExpanded: true,
            dropdownColor: const Color(0xFF1C132E),
            value: _findMatchingFile(audioFiles, _welcomeFile),
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
            ),
            items: audioFiles.map((file) {
              return DropdownMenuItem<String>(
                value: file,
                child: Text(file, overflow: TextOverflow.ellipsis),
              );
            }).toList(),
            onChanged: (val) {
              setState(() {
                _welcomeFile = val;
              });
            },
          ),
          const SizedBox(height: 10),

          // Test Play Button
          ElevatedButton.icon(
            onPressed: pState.isTesting
                ? null
                : () async {
                    final ok = await ref.read(presenceProvider.notifier).testWelcome();
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(ok ? 'تم إرسال أمر تجربة الترحيب' : 'فشل تشغيل الصوت'),
                          backgroundColor: ok ? Colors.green : Colors.redAccent,
                        ),
                      );
                    }
                  },
            icon: pState.isTesting
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.volume_up, size: 18),
            label: Text('تجربة تشغيل صوت الترحيب الآن', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF8E44AD),
              foregroundColor: Colors.white,
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
            ),
          ),
          const SizedBox(height: 20),

          // Volume Slider
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text('مستوى صوت الترحيب:', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
              Text('$_volume / 30', style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold)),
            ],
          ),
          Slider(
            value: _volume.toDouble().clamp(0.0, 30.0),
            min: 0,
            max: 30,
            divisions: 30,
            activeColor: const Color(0xFF00F2FE),
            onChanged: (val) {
              setState(() {
                _volume = val.round();
              });
            },
          ),
          const SizedBox(height: 16),

          // Cooldown and Vacate
          Row(
            children: [
              Expanded(
                child: TextField(
                  controller: _cooldownController,
                  keyboardType: TextInputType.number,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    labelText: 'مهلة التكرار (دقيقة)',
                    labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
                    helperText: 'لمنع تكرار الترحيب سريعاً',
                    helperStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 10),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.04),
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
                  ),
                ),
              ),
              const SizedBox(width: 12),
              Expanded(
                child: TextField(
                  controller: _vacateController,
                  keyboardType: TextInputType.number,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    labelText: 'مهلة الغياب (دقيقة)',
                    labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
                    helperText: 'قبل اعتبار الشخص غائباً',
                    helperStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 10),
                    filled: true,
                    fillColor: Colors.white.withOpacity(0.04),
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(14)),
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          // Quiet Hours
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: Text('ساعات الهدوء (عدم الإزعاج)', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
            subtitle: Text('كتم صوت الترحيب في أوقات محددة (مثل الليل)', style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12)),
            value: _quietEnabled,
            activeColor: const Color(0xFF00F2FE),
            onChanged: (val) {
              setState(() {
                _quietEnabled = val;
              });
            },
          ),
          if (_quietEnabled) ...[
            const SizedBox(height: 8),
            Row(
              children: [
                Expanded(
                  child: ListTile(
                    tileColor: Colors.white.withOpacity(0.04),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    title: Text('من الساعة:', style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12)),
                    trailing: Text(
                      ':',
                      style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                    onTap: () async {
                      final picked = await showTimePicker(context: context, initialTime: _quietStart);
                      if (picked != null) {
                        setState(() {
                          _quietStart = picked;
                        });
                      }
                    },
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: ListTile(
                    tileColor: Colors.white.withOpacity(0.04),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    title: Text('إلى الساعة:', style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12)),
                    trailing: Text(
                      ':',
                      style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold, fontSize: 14),
                    ),
                    onTap: () async {
                      final picked = await showTimePicker(context: context, initialTime: _quietEnd);
                      if (picked != null) {
                        setState(() {
                          _quietEnd = picked;
                        });
                      }
                    },
                  ),
                ),
              ],
            ),
          ],
          const SizedBox(height: 28),

          // Save Button
          ElevatedButton.icon(
            onPressed: pState.isSaving ? null : _handleSave,
            icon: pState.isSaving
                ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text(
              pState.isSaving ? 'جاري الحفظ...' : 'حفظ إعدادات كشف الحضور',
              style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 15),
            ),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00F2FE),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _WifiCard extends ConsumerStatefulWidget {
  const _WifiCard();

  @override
  ConsumerState<_WifiCard> createState() => _WifiCardState();
}

class _WifiCardState extends ConsumerState<_WifiCard> {
  @override
  Widget build(BuildContext context) {
    final wifi = ref.watch(wifiProvider);
    final status = wifi.status;
    final networks = wifi.networks;

    final currentSsid = status['ssid'] ?? 'غير متصل';
    final currentIp = status['ip'] ?? '0.0.0.0';
    final currentSignal = status['signal'] ?? 0;

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.wifi, color: Color(0xFF00FFCC), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'إعدادات الشبكة والـ Wi-Fi',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(16),
              border: Border.all(color: Colors.white.withOpacity(0.08)),
            ),
            child: Column(
              children: [
                _buildInfoRow('الشبكة الحالية', currentSsid, Colors.white70),
                const Divider(color: Colors.white10, height: 16),
                _buildInfoRow('عنوان IP الخاص بالجهاز', currentIp, const Color(0xFF00F2FE)),
                const Divider(color: Colors.white10, height: 16),
                _buildInfoRow('قوة الإشارة', '$currentSignal dBm', const Color(0xFF00FFCC)),
              ],
            ),
          ),
          const SizedBox(height: 20),

          ElevatedButton.icon(
            onPressed: wifi.isScanning ? null : () async {
              await ref.read(wifiProvider.notifier).scan();
            },
            icon: wifi.isScanning 
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.search),
            label: Text(
              wifi.isScanning ? 'جاري البحث عن الشبكات القريبة...' : 'البحث عن شبكات Wi-Fi المتاحة',
              style: GoogleFonts.cairo(fontWeight: FontWeight.bold),
            ),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00F2FE),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: () {
              showDialog(
                context: context,
                builder: (dialogCtx) => Directionality(
                  textDirection: TextDirection.rtl,
                  child: Dialog(
                    backgroundColor: const Color(0xFF1C132E),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(24),
                      side: BorderSide(color: Colors.white.withOpacity(0.1)),
                    ),
                    insetPadding: const EdgeInsets.symmetric(horizontal: 20, vertical: 24),
                    child: Container(
                      width: 520,
                      constraints: BoxConstraints(
                        maxHeight: MediaQuery.of(context).size.height * 0.85,
                      ),
                      child: SingleChildScrollView(
                        padding: const EdgeInsets.all(16),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Row(
                              children: [
                                const Icon(Icons.cell_tower, color: Color(0xFF00F2FE), size: 24),
                                const SizedBox(width: 10),
                                Expanded(
                                  child: Text(
                                    'نقطة اتصال الطوارئ (Setup AP)',
                                    style: GoogleFonts.cairo(
                                      color: Colors.white,
                                      fontSize: 18,
                                      fontWeight: FontWeight.bold,
                                    ),
                                  ),
                                ),
                                IconButton(
                                  icon: const Icon(Icons.close, color: Colors.white54, size: 22),
                                  onPressed: () => Navigator.of(dialogCtx).pop(),
                                ),
                              ],
                            ),
                            const Divider(color: Colors.white10),
                            const SizedBox(height: 10),
                            const _ApSettingsCard(),
                          ],
                        ),
                      ),
                    ),
                  ),
                ),
              );
            },
            icon: const Icon(Icons.cell_tower, color: Color(0xFF00FFCC), size: 20),
            label: Text(
              'إعدادات نقطة اتصال الطوارئ (Setup AP)',
              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
            ),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 13),
              side: const BorderSide(color: Color(0xFF00FFCC), width: 1.2),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
            ),
          ),

          if (wifi.error != null) ...[
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.redAccent.withOpacity(0.15),
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: Colors.redAccent.withOpacity(0.3)),
              ),
              child: Text(
                wifi.error!,
                style: GoogleFonts.cairo(color: Colors.redAccent, fontSize: 12),
                textAlign: TextAlign.center,
              ),
            ),
          ],

          if (networks.isNotEmpty) ...[
            const SizedBox(height: 20),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(
                  'الشبكات المكتشفة (${networks.length}):',
                  style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 14, fontWeight: FontWeight.bold),
                ),
                Text(
                  'انقر لاختيار الشبكة',
                  style: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
                ),
              ],
            ),
            const SizedBox(height: 10),
            ListView.separated(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: networks.length,
              separatorBuilder: (context, index) => const SizedBox(height: 8),
              itemBuilder: (context, index) {
                final net = networks[index];
                final ssid = net['ssid'] as String? ?? 'مجهول';
                final rssi = (net['rssi'] as num?)?.toInt() ?? -100;
                final enc = (net['enc'] as num?)?.toInt() ?? 1;
                final secure = net['secure'] as bool? ?? (enc > 0);

                return Container(
                  decoration: BoxDecoration(
                    color: const Color(0x7316132C),
                    borderRadius: BorderRadius.circular(16),
                    border: Border.all(color: Colors.white.withOpacity(0.08)),
                  ),
                  child: ListTile(
                    leading: Icon(
                      rssi > -60 ? Icons.wifi : rssi > -80 ? Icons.wifi_2_bar : Icons.wifi_1_bar,
                      color: const Color(0xFF00FFCC).withOpacity(0.8),
                    ),
                    title: Text(ssid, style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold)),
                    subtitle: Text('قوة الإشارة: $rssi dBm ${secure ? "(مشفرة)" : "(مفتوحة)"}',
                        style: const TextStyle(color: Colors.white30, fontSize: 11)),
                    trailing: Icon(secure ? Icons.lock : Icons.lock_open, color: Colors.white38, size: 18),
                    onTap: () {
                      _showConnectDialog(context, ssid);
                    },
                  ),
                );
              },
            ),
          ],
        ],
      ),
    );
  }

  Widget _buildInfoRow(String title, String value, Color valueColor) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Expanded(
          child: Text(
            title, 
            style: GoogleFonts.cairo(color: Colors.white38, fontSize: 13),
            overflow: TextOverflow.ellipsis,
          ),
        ),
        const SizedBox(width: 10),
        Text(
          value, 
          style: GoogleFonts.cairo(color: valueColor, fontWeight: FontWeight.bold, fontSize: 13),
        ),
      ],
    );
  }

  void _showConnectDialog(BuildContext context, String ssid) {
    final passwordController = TextEditingController();
    showDialog(
      context: context,
      builder: (context) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('الاتصال بالشبكة: $ssid', style: GoogleFonts.cairo(color: Colors.white, fontSize: 16)),
          content: TextField(
            controller: passwordController,
            obscureText: true,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              hintText: 'كلمة مرور الشبكة',
              hintStyle: GoogleFonts.cairo(color: Colors.white30),
              enabledBorder: const UnderlineInputBorder(borderSide: BorderSide(color: Colors.white30)),
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () async {
                final password = passwordController.text.trim();
                Navigator.pop(context);

                final success = await ref.read(wifiProvider.notifier).saveAndConnect(ssid, password);
                if (context.mounted) {
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text(success ? 'تم إرسال أمر الاتصال بنجاح. قد يتغير IP الجهاز.' : 'فشل حفظ أو إرسال أمر الاتصال'),
                      backgroundColor: success ? Colors.green : Colors.redAccent,
                    ),
                  );
                }
              },
              style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              child: Text('اتصال', style: GoogleFonts.cairo()),
            ),
          ],
        ),
      ),
    );
  }
}

class _PasswordCard extends ConsumerStatefulWidget {
  const _PasswordCard();

  @override
  ConsumerState<_PasswordCard> createState() => _PasswordCardState();
}

class _PasswordCardState extends ConsumerState<_PasswordCard> {
  final _oldPassController = TextEditingController();
  final _newPassController = TextEditingController();
  final _confirmPassController = TextEditingController();
  bool _isSaving = false;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.lock_outline_rounded, color: Color(0xFF8E44AD), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'تغيير كلمة مرور الجهاز',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),

          TextField(
            controller: _oldPassController,
            obscureText: true,
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
            decoration: InputDecoration(
              labelText: 'كلمة المرور الحالية',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13, fontWeight: FontWeight.bold),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 12),

          TextField(
            controller: _newPassController,
            obscureText: true,
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
            decoration: InputDecoration(
              labelText: 'كلمة المرور الجديدة',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13, fontWeight: FontWeight.bold),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 12),

          TextField(
            controller: _confirmPassController,
            obscureText: true,
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
            decoration: InputDecoration(
              labelText: 'تأكيد كلمة المرور الجديدة',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13, fontWeight: FontWeight.bold),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 20),

          ElevatedButton.icon(
            onPressed: _isSaving ? null : () async {
              final oldPass = _oldPassController.text.trim();
              final newPass = _newPassController.text.trim();
              final confirmPass = _confirmPassController.text.trim();

              if (oldPass.isEmpty || newPass.isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('الرجاء ملء جميع الحقول'), backgroundColor: Colors.redAccent),
                );
                return;
              }

              if (newPass != confirmPass) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('كلمة المرور الجديدة غير متطابقة مع التأكيد'), backgroundColor: Colors.redAccent),
                );
                return;
              }

              setState(() {
                _isSaving = true;
              });

              final api = ref.read(apiServiceProvider);
              final result = await api.changePassword(oldPass, newPass);

              setState(() {
                _isSaving = false;
              });

              if (context.mounted) {
                final success = result['ok'] == true;
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(success ? 'تم تغيير كلمة المرور بنجاح!' : (result['error'] ?? 'فشل تغيير كلمة المرور')),
                    backgroundColor: success ? Colors.green : Colors.redAccent,
                  ),
                );

                if (success) {
                  _oldPassController.clear();
                  _newPassController.clear();
                  _confirmPassController.clear();
                }
              }
            },
            icon: _isSaving 
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ كلمة المرور الجديدة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF8E44AD),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}
class _NetworkSettingsCard extends ConsumerStatefulWidget {
  const _NetworkSettingsCard();

  @override
  ConsumerState<_NetworkSettingsCard> createState() => _NetworkSettingsCardState();
}

class _NetworkSettingsCardState extends ConsumerState<_NetworkSettingsCard> {
  bool _dhcpEnabled = true;
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();
  final _ipController = TextEditingController();
  final _maskController = TextEditingController();
  final _gatewayController = TextEditingController();
  final _dnsController = TextEditingController();
  bool _isSaving = false;
  bool _initialized = false;

  @override
  Widget build(BuildContext context) {
    final wifiState = ref.watch(wifiProvider);
    final status = wifiState.status;

    if (!_initialized && status.isNotEmpty) {
      _dhcpEnabled = status['dhcp'] == true || status['dhcp'] == 1 || status['dhcp'] == '1';
      _ssidController.text = status['savedSsid'] ?? status['ssid'] ?? '';
      _ipController.text = status['staticIp'] ?? status['ip'] ?? '';
      _maskController.text = status['subnet'] ?? status['mask'] ?? '255.255.255.0';
      _gatewayController.text = status['gateway'] ?? '';
      _dnsController.text = status['dns'] ?? '8.8.8.8';
      _initialized = true;
    }

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.settings_ethernet, color: Color(0xFF00F2FE), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'ضبط بروتوكول الشبكة DHCP / Static IP',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),
          TextField(
            controller: _ssidController,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              labelText: 'اسم شبكة Wi-Fi (SSID)',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _passwordController,
            style: const TextStyle(color: Colors.white),
            obscureText: true,
            decoration: InputDecoration(
              labelText: 'كلمة مرور شبكة Wi-Fi',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 12),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _dhcpEnabled,
            activeColor: const Color(0xFF00F2FE),
            title: Text('تفعيل DHCP (الحصول على IP تلقائياً)', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) {
              setState(() {
                _dhcpEnabled = val;
              });
            },
          ),
          if (!_dhcpEnabled) ...[
            const SizedBox(height: 12),
            TextField(
              controller: _ipController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'عنوان IP الثابت',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _maskController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'قناع الشبكة (Subnet Mask)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _gatewayController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'البوابة الافتراضية (Gateway)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _dnsController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'خادم النطاقات DNS',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
                ),
              ),
            ),
          ],
          const SizedBox(height: 20),
          ElevatedButton.icon(
            onPressed: _isSaving ? null : () async {
              setState(() {
                _isSaving = true;
              });
              if (_ssidController.text.trim().isEmpty || _passwordController.text.trim().isEmpty) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('الرجاء إدخال اسم الشبكة وكلمة المرور لحفظ الإعدادات بأمان'), backgroundColor: Colors.redAccent),
                );
                setState(() {
                  _isSaving = false;
                });
                return;
              }
              final success = await ref.read(apiServiceProvider).saveNetworkConfig(
                ssid: _ssidController.text.trim(),
                password: _passwordController.text.trim(),
                dhcp: _dhcpEnabled,
                ip: _ipController.text.trim(),
                mask: _maskController.text.trim(),
                gateway: _gatewayController.text.trim(),
                dns: _dnsController.text.trim(),
              );
              setState(() {
                _isSaving = false;
              });
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(success ? 'تم حفظ إعدادات الشبكة بنجاح' : 'فشل حفظ إعدادات الشبكة'),
                    backgroundColor: success ? Colors.green : Colors.redAccent,
                  ),
                );
              }
            },
            icon: _isSaving
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ إعدادات الشبكة', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00F2FE),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _ApSettingsCard extends ConsumerStatefulWidget {
  const _ApSettingsCard();

  @override
  ConsumerState<_ApSettingsCard> createState() => _ApSettingsCardState();
}

class _ApSettingsCardState extends ConsumerState<_ApSettingsCard> {
  final _ssidController = TextEditingController(text: 'VivoSmart-Setup');
  final _passController = TextEditingController();
  bool _obscurePass = true;
  bool _isLoading = true;
  bool _isSaving = false;
  bool _isActive = false;
  int _stations = 0;
  bool _hasPass = false;

  @override
  void initState() {
    super.initState();
    _loadApConfig();
  }

  Future<void> _loadApConfig() async {
    setState(() => _isLoading = true);
    final cfg = await ref.read(apiServiceProvider).getApConfig();
    if (mounted) {
      setState(() {
        _isLoading = false;
        if (cfg.containsKey('ssid') && cfg['ssid'].toString().isNotEmpty) {
          _ssidController.text = cfg['ssid'].toString();
        }
        _hasPass = cfg['hasPass'] == true;
        _isActive = cfg['active'] == true;
        _stations = cfg['stations'] is int ? cfg['stations'] : 0;
      });
    }
  }

  Future<void> _saveApConfig() async {
    final ssid = _ssidController.text.trim();
    final pass = _passController.text.trim();

    if (ssid.length < 3) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('اسم نقطة الاتصال يجب ألا يقل عن 3 أحرف'),
          backgroundColor: Colors.redAccent,
        ),
      );
      return;
    }
    if (pass.isNotEmpty && pass.length < 8) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('كلمة المرور يجب ألا تقل عن 8 أحرف أو اتركها فارغة لشبكة مفتوحة'),
          backgroundColor: Colors.redAccent,
        ),
      );
      return;
    }

    setState(() => _isSaving = true);
    final ok = await ref.read(apiServiceProvider).saveApConfig(ssid: ssid, pass: pass);
    setState(() => _isSaving = false);

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(ok ? 'تم حفظ إعدادات نقطة الاتصال (Setup AP) بنجاح' : 'فشل حفظ إعدادات نقطة الاتصال'),
          backgroundColor: ok ? Colors.green : Colors.redAccent,
        ),
      );
      if (ok) {
        _loadApConfig();
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.cell_tower, color: Color(0xFF00F2FE), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'نقطة اتصال الطوارئ (Setup AP)',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                ),
              ),
              if (_isLoading)
                const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Color(0xFF00F2FE), strokeWidth: 2))
              else
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                  decoration: BoxDecoration(
                    color: _isActive ? const Color(0x332ECC71) : Colors.white10,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: _isActive ? const Color(0xFF2ECC71) : Colors.white24),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        width: 8,
                        height: 8,
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          color: _isActive ? const Color(0xFF2ECC71) : Colors.white38,
                        ),
                      ),
                      const SizedBox(width: 6),
                      Text(
                        _isActive ? 'نشطة ($_stations متصل)' : 'متوقفة',
                        style: GoogleFonts.cairo(
                          color: _isActive ? const Color(0xFF2ECC71) : Colors.white60,
                          fontSize: 11,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                    ],
                  ),
                ),
            ],
          ),
          const SizedBox(height: 10),
          Text(
            'تعمل هذه الشبكة تلقائياً عند انقطاع الواي فاي أو الإنترنت للتحكم في الجهاز بالكامل محلياً عبر 192.168.4.1 مع بوابة الدخول المباشر (Captive Portal).',
            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, height: 1.5),
          ),
          const SizedBox(height: 16),
          TextField(
            controller: _ssidController,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              labelText: 'اسم الشبكة (SSID)',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
              hintText: 'VivoSmart-Setup',
              hintStyle: const TextStyle(color: Colors.white24),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _passController,
            obscureText: _obscurePass,
            style: const TextStyle(color: Colors.white),
            decoration: InputDecoration(
              labelText: 'كلمة المرور',
              labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
              hintText: _hasPass ? '•••••••• (محمية بكلمة مرور)' : 'بدون كلمة مرور (شبكة مفتوحة)',
              hintStyle: TextStyle(color: _hasPass ? Colors.white54 : Colors.white24),
              suffixIcon: IconButton(
                icon: Icon(_obscurePass ? Icons.visibility_off : Icons.visibility, color: Colors.white54),
                onPressed: () => setState(() => _obscurePass = !_obscurePass),
              ),
              filled: true,
              fillColor: Colors.white.withOpacity(0.04),
              border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
              enabledBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
              ),
              focusedBorder: OutlineInputBorder(
                borderRadius: BorderRadius.circular(16),
                borderSide: const BorderSide(color: Color(0xFF00F2FE), width: 1.5),
              ),
            ),
          ),
          const SizedBox(height: 20),
          ElevatedButton.icon(
            onPressed: _isSaving ? null : _saveApConfig,
            icon: _isSaving
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ إعدادات نقطة الاتصال (Setup AP)', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00F2FE),
              foregroundColor: Colors.black87,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _DdnsSettingsCard extends ConsumerStatefulWidget {
  const _DdnsSettingsCard();

  @override
  ConsumerState<_DdnsSettingsCard> createState() => _DdnsSettingsCardState();
}

class _DdnsSettingsCardState extends ConsumerState<_DdnsSettingsCard> {
  bool _ddnsEnabled = false;
  String _ddnsProvider = 'noip';
  final _ddnsDomainController = TextEditingController();
  final _ddnsUsernameController = TextEditingController();
  final _ddnsPasswordController = TextEditingController();
  bool _useIPv6 = true;
  bool _hasPass = false;
  bool _obscurePass = true;
  String _statusText = 'غير مفعل';
  String _lastUpdateTime = '';
  String _globalIPv6 = '';
  bool _isLoading = true;
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _loadDdnsConfig();
  }

  Future<void> _loadDdnsConfig() async {
    try {
      final data = await ref.read(apiServiceProvider).getDdnsConfig();
      if (!mounted) return;
      if (data.isNotEmpty) {
        final prov = (data['provider'] ?? 'noip').toString();
        setState(() {
          _ddnsEnabled = data['enabled'] == true || data['enabled'] == 1 || data['enabled'] == '1';
          _ddnsProvider = ['noip', 'duckdns', 'dynu'].contains(prov) ? prov : 'noip';
          _ddnsDomainController.text = (data['domain'] ?? '').toString();
          _ddnsUsernameController.text = (data['user'] ?? '').toString();
          _hasPass = data['hasPass'] == true;
          if (_hasPass) {
            _ddnsPasswordController.text = '********';
          }
          _useIPv6 = data['useIPv6'] != false;
          _statusText = (data['status'] ?? (_ddnsEnabled ? 'مفعل' : 'غير مفعل')).toString();
          _lastUpdateTime = (data['lastUpdateTime'] ?? '').toString();
          _globalIPv6 = (data['globalIPv6'] ?? '').toString();
          _isLoading = false;
        });
      } else {
        setState(() => _isLoading = false);
      }
    } catch (_) {
      if (mounted) setState(() => _isLoading = false);
    }
  }

  @override
  void dispose() {
    _ddnsDomainController.dispose();
    _ddnsUsernameController.dispose();
    _ddnsPasswordController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final isOk = _statusText.contains('✅');
    final isErr = _statusText.contains('❌');

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.public, color: Color(0xFF8E44AD), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'الوصول العالمي المباشر و DDNS (No-IP / DuckDNS / Dynu)',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.refresh, color: Colors.white54, size: 20),
                tooltip: 'تحديث الحالة',
                onPressed: _loadDdnsConfig,
              ),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            'يدعم التحديث التلقائي لعنوان IPv6 العالمي المباشر (لتخطي جدار CGNAT على شبكات الجوال والهوتسبوت بدون Port Forwarding) بالإضافة إلى IPv4.',
            style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
          ),
          const SizedBox(height: 14),
          if (_globalIPv6.length > 4) ...[
            Container(
              padding: const EdgeInsets.all(12),
              margin: const EdgeInsets.only(bottom: 14),
              decoration: BoxDecoration(
                color: const Color(0xFF2ECC71).withOpacity(0.12),
                borderRadius: BorderRadius.circular(14),
                border: Border.all(color: const Color(0xFF2ECC71).withOpacity(0.35)),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      const Icon(Icons.link, color: Color(0xFF2ECC71), size: 18),
                      const SizedBox(width: 6),
                      Expanded(
                        child: Text(
                          'رابط الوصول العالمي المباشر للجهاز (IPv6):',
                          style: GoogleFonts.cairo(
                            color: const Color(0xFF2ECC71),
                            fontWeight: FontWeight.bold,
                            fontSize: 12,
                          ),
                        ),
                      ),
                      IconButton(
                        icon: const Icon(Icons.copy, color: Color(0xFF00F2FE), size: 18),
                        tooltip: 'نسخ عنوان IPv6',
                        constraints: const BoxConstraints(),
                        padding: const EdgeInsets.all(4),
                        onPressed: () {
                          Clipboard.setData(ClipboardData(text: 'http://[$_globalIPv6]/'));
                          ScaffoldMessenger.of(context).showSnackBar(
                            const SnackBar(
                              content: Text('تم نسخ رابط IPv6 العالمي إلى الحافظة'),
                              backgroundColor: Colors.green,
                            ),
                          );
                        },
                      ),
                    ],
                  ),
                  const SizedBox(height: 4),
                  SelectableText(
                    'http://[$_globalIPv6]/',
                    textDirection: TextDirection.ltr,
                    style: const TextStyle(
                      color: Color(0xFF00F2FE),
                      fontFamily: 'monospace',
                      fontSize: 12.5,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ],
              ),
            ),
          ],
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: isOk
                  ? const Color(0xFF28A745).withOpacity(0.15)
                  : (isErr
                      ? Colors.redAccent.withOpacity(0.15)
                      : (_ddnsEnabled ? const Color(0xFFF39C12).withOpacity(0.15) : Colors.white.withOpacity(0.05))),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(
                color: isOk
                    ? const Color(0xFF28A745).withOpacity(0.4)
                    : (isErr
                        ? Colors.redAccent.withOpacity(0.4)
                        : (_ddnsEnabled ? const Color(0xFFF39C12).withOpacity(0.4) : Colors.white12)),
              ),
            ),
            child: Row(
              children: [
                Text(
                  'حالة DDNS: ',
                  style: GoogleFonts.cairo(color: Colors.white70, fontSize: 12, fontWeight: FontWeight.bold),
                ),
                Expanded(
                  child: Text(
                    _lastUpdateTime.isNotEmpty ? '$_statusText ($_lastUpdateTime)' : _statusText,
                    style: GoogleFonts.cairo(
                      color: isOk
                          ? const Color(0xFF2ECC71)
                          : (isErr ? Colors.redAccent : (_ddnsEnabled ? const Color(0xFFF39C12) : Colors.white60)),
                      fontSize: 12,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 10),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _ddnsEnabled,
            activeColor: const Color(0xFF8E44AD),
            title: Text('تفعيل خدمة DDNS', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold)),
            onChanged: (val) {
              setState(() {
                _ddnsEnabled = val;
              });
            },
          ),
          if (_ddnsEnabled) ...[
            const SizedBox(height: 12),
            DropdownButtonFormField<String>(
              isExpanded: true,
              dropdownColor: const Color(0xFF1C132E),
              value: _ddnsProvider,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'مزود الخدمة (Provider)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
                ),
              ),
              items: const [
                DropdownMenuItem(value: 'noip', child: Text('No-IP (ddns.net)')),
                DropdownMenuItem(value: 'duckdns', child: Text('DuckDNS (duckdns.org)')),
                DropdownMenuItem(value: 'dynu', child: Text('Dynu DNS (dynu.net)')),
              ],
              onChanged: (val) {
                setState(() {
                  _ddnsProvider = val ?? 'noip';
                });
              },
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _ddnsDomainController,
              style: const TextStyle(color: Colors.white),
              textDirection: TextDirection.ltr,
              decoration: InputDecoration(
                labelText: 'النطاق (Domain)',
                hintText: 'example.ddns.net أو example.duckdns.org',
                hintStyle: const TextStyle(color: Colors.white24, fontSize: 12),
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _ddnsUsernameController,
              style: const TextStyle(color: Colors.white),
              textDirection: TextDirection.ltr,
              decoration: InputDecoration(
                labelText: 'اسم المستخدم (أو اتركه فارغاً لـ DuckDNS)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _ddnsPasswordController,
              obscureText: _obscurePass,
              style: const TextStyle(color: Colors.white),
              textDirection: TextDirection.ltr,
              decoration: InputDecoration(
                labelText: 'كلمة المرور (أو Token لـ DuckDNS)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                suffixIcon: IconButton(
                  icon: Icon(_obscurePass ? Icons.visibility_off : Icons.visibility, color: Colors.white54),
                  onPressed: () => setState(() => _obscurePass = !_obscurePass),
                ),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF8E44AD), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 8),
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              value: _useIPv6,
              activeColor: const Color(0xFF00F2FE),
              title: Text(
                'ربط النطاق بعنوان IPv6 العالمي للجهاز تلقائياً (أفضل للهوتسبوت و 4G/5G)',
                style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
              ),
              onChanged: (val) => setState(() => _useIPv6 = val),
            ),
          ],
          const SizedBox(height: 16),
          ElevatedButton.icon(
            onPressed: _isSaving
                ? null
                : () async {
                    setState(() {
                      _isSaving = true;
                    });
                    final success = await ref.read(apiServiceProvider).saveDdnsConfig(
                          enabled: _ddnsEnabled,
                          provider: _ddnsProvider,
                          domain: _ddnsDomainController.text.trim(),
                          user: _ddnsUsernameController.text.trim(),
                          pass: _ddnsPasswordController.text.trim(),
                          useIPv6: _useIPv6,
                        );
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(success ? 'تم حفظ إعدادات DDNS وجاري التحديث...' : 'فشل حفظ إعدادات DDNS'),
                          backgroundColor: success ? Colors.green : Colors.redAccent,
                        ),
                      );
                    }
                    await Future.delayed(const Duration(milliseconds: 2500));
                    await _loadDdnsConfig();
                    if (mounted) {
                      setState(() {
                        _isSaving = false;
                      });
                    }
                  },
            icon: _isSaving
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ وتحديث DDNS الآن', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF8E44AD),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _TelegramSettingsCard extends ConsumerStatefulWidget {
  const _TelegramSettingsCard();

  @override
  ConsumerState<_TelegramSettingsCard> createState() => _TelegramSettingsCardState();
}

class _TelegramSettingsCardState extends ConsumerState<_TelegramSettingsCard> {
  bool _tgEnabled = false;
  final _tgTokenController = TextEditingController();
  final _tgChatIdController = TextEditingController();
  bool _hasToken = false;
  bool _obscureToken = true;
  String _statusText = 'غير مفعل';
  bool _isSaving = false;

  @override
  void initState() {
    super.initState();
    _loadTelegramConfig();
  }

  Future<void> _loadTelegramConfig() async {
    try {
      final data = await ref.read(apiServiceProvider).getTelegramConfig();
      if (!mounted) return;
      if (data.isNotEmpty) {
        setState(() {
          _tgEnabled = data['enabled'] == true || data['enabled'] == 1 || data['enabled'] == '1';
          _hasToken = data['hasToken'] == true;
          if (_hasToken) {
            _tgTokenController.text = '********';
          }
          _tgChatIdController.text = (data['chatId'] ?? '').toString();
          _statusText = (data['status'] ?? (_tgEnabled ? 'مفعل' : 'غير مفعل')).toString();
        });
      }
    } catch (_) {}
  }

  @override
  void dispose() {
    _tgTokenController.dispose();
    _tgChatIdController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final isOk = _statusText.contains('✅');
    final isErr = _statusText.contains('❌');

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.send_rounded, color: Color(0xFF0088CC), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'التحكم عن بعد من أي مكان في العالم (Telegram Bot)',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                ),
              ),
              IconButton(
                icon: const Icon(Icons.refresh, color: Colors.white54, size: 20),
                tooltip: 'تحديث حالة البوت',
                onPressed: _loadTelegramConfig,
              ),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            'يعمل من خلف أي شبكة أو هوتسبوت (حتى مع CGNAT وبدون IP عام أو فتح بورتات)! أنشئ بوت مجاني عبر @BotFather في تيليجرام وضع التوكن هنا للتحكم الكامل في الجهاز من أي مكان في العالم.',
            style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
          ),
          const SizedBox(height: 14),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
            decoration: BoxDecoration(
              color: isOk
                  ? const Color(0xFF28A745).withOpacity(0.15)
                  : (isErr
                      ? Colors.redAccent.withOpacity(0.15)
                      : (_tgEnabled ? const Color(0xFF0088CC).withOpacity(0.15) : Colors.white.withOpacity(0.05))),
              borderRadius: BorderRadius.circular(12),
              border: Border.all(
                color: isOk
                    ? const Color(0xFF28A745).withOpacity(0.4)
                    : (isErr
                        ? Colors.redAccent.withOpacity(0.4)
                        : (_tgEnabled ? const Color(0xFF0088CC).withOpacity(0.4) : Colors.white12)),
              ),
            ),
            child: Row(
              children: [
                Text(
                  'حالة البوت: ',
                  style: GoogleFonts.cairo(color: Colors.white70, fontSize: 12, fontWeight: FontWeight.bold),
                ),
                Expanded(
                  child: Text(
                    _statusText,
                    style: GoogleFonts.cairo(
                      color: isOk
                          ? const Color(0xFF2ECC71)
                          : (isErr ? Colors.redAccent : (_tgEnabled ? const Color(0xFF00F2FE) : Colors.white60)),
                      fontSize: 12,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 10),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _tgEnabled,
            activeColor: const Color(0xFF0088CC),
            title: Text(
              'تفعيل التحكم عبر تيليجرام',
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
            ),
            onChanged: (val) => setState(() => _tgEnabled = val),
          ),
          if (_tgEnabled) ...[
            const SizedBox(height: 12),
            TextField(
              controller: _tgTokenController,
              obscureText: _obscureToken,
              style: const TextStyle(color: Colors.white),
              textDirection: TextDirection.ltr,
              decoration: InputDecoration(
                labelText: 'توكن البوت (Bot Token من @BotFather)',
                hintText: '123456789:ABCdefGHIjklMNOpqrSTUvwxYZ',
                hintStyle: const TextStyle(color: Colors.white24, fontSize: 12),
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                suffixIcon: IconButton(
                  icon: Icon(_obscureToken ? Icons.visibility_off : Icons.visibility, color: Colors.white54),
                  onPressed: () => setState(() => _obscureToken = !_obscureToken),
                ),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF0088CC), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _tgChatIdController,
              style: const TextStyle(color: Colors.white),
              textDirection: TextDirection.ltr,
              decoration: InputDecoration(
                labelText: 'معرف المحادثة المسموح له (Chat ID)',
                hintText: 'سيتم تعبئته تلقائياً عند إرسال /start للبوت',
                hintStyle: GoogleFonts.cairo(color: Colors.white30, fontSize: 11),
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 12.5),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF0088CC), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.white.withOpacity(0.03),
                borderRadius: BorderRadius.circular(14),
                border: Border.all(color: Colors.white10),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    '📋 أوامر التحكم المتاحة في تيليجرام:',
                    style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontWeight: FontWeight.bold, fontSize: 12),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    '• /status (عرض الحالة والروابط)   • /prayers (مواقيت اليوم)\n'
                    '• /stop (إيقاف الصوت فوراً)        • /vol 25 (تغيير الصوت 0-30)\n'
                    '• /eid_on و /eid_off (وضع العيد)  • /takbeer (تشغيل التكبيرات)\n'
                    '• /reboot (إعادة تشغيل الجهاز)',
                    style: GoogleFonts.cairo(color: Colors.white70, fontSize: 11, height: 1.6),
                  ),
                ],
              ),
            ),
          ],
          const SizedBox(height: 16),
          ElevatedButton.icon(
            onPressed: _isSaving
                ? null
                : () async {
                    setState(() => _isSaving = true);
                    final success = await ref.read(apiServiceProvider).saveTelegramConfig(
                          enabled: _tgEnabled,
                          token: _tgTokenController.text.trim(),
                          chatId: _tgChatIdController.text.trim(),
                        );
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(success
                              ? 'تم حفظ إعدادات بوت تيليجرام للتحكم عن بعد'
                              : 'فشل حفظ إعدادات تيليجرام'),
                          backgroundColor: success ? Colors.green : Colors.redAccent,
                        ),
                      );
                    }
                    await Future.delayed(const Duration(milliseconds: 2000));
                    await _loadTelegramConfig();
                    if (mounted) {
                      setState(() => _isSaving = false);
                    }
                  },
            icon: _isSaving
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ إعدادات تيليجرام', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF0088CC),
              foregroundColor: Colors.white,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _OtaUpdateCard extends ConsumerStatefulWidget {
  const _OtaUpdateCard();

  @override
  ConsumerState<_OtaUpdateCard> createState() => _OtaUpdateCardState();
}

class _OtaUpdateCardState extends ConsumerState<_OtaUpdateCard> {
  String? _selectedFilePath;
  String? _selectedFileName;
  bool _isUploading = false;
  double _progress = 0.0;
  String _statusText = '';

  Future<void> _pickBinFile() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: ['bin'],
    );
    if (result != null && result.files.single.path != null) {
      setState(() {
        _selectedFilePath = result.files.single.path!;
        _selectedFileName = result.files.single.name;
        _statusText = '';
        _progress = 0.0;
      });
    }
  }

  Future<void> _startOtaUpload() async {
    if (_selectedFilePath == null || _selectedFileName == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('يرجى اختيار ملف التحديث (.bin) أولاً'), backgroundColor: Colors.orange),
      );
      return;
    }

    final isLittleFs = _selectedFileName!.toLowerCase().contains('littlefs') ||
        _selectedFileName!.toLowerCase().contains('spiffs');

    setState(() {
      _isUploading = true;
      _progress = 0.0;
      _statusText = isLittleFs
          ? 'جاري رفع تحديث ملفات النظام (LittleFS)...'
          : 'جاري رفع تحديث السوفتوير (Firmware)...';
    });

    ref.read(statusPollingProvider.notifier).stopPolling();
    try {
      final ok = await ref.read(apiServiceProvider).uploadOtaUpdate(
        filePath: _selectedFilePath!,
        filename: _selectedFileName!,
        onSendProgress: (sent, total) {
          if (mounted && total > 0) {
            setState(() {
              _progress = (sent / total).clamp(0.0, 1.0);
              final pct = (_progress * 100).round();
              _statusText = 'جاري رفع التحديث: $pct%';
            });
          }
        },
      );

      if (mounted) {
        setState(() {
          _isUploading = false;
          if (ok) {
            _progress = 1.0;
            _statusText = '✅ تم رفع التحديث بنجاح! جاري إعادة تشغيل الجهاز...';
          } else {
            _statusText = '❌ فشل رفع التحديث، يرجى التحقق من الملف والاتصال.';
          }
        });
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(ok
                ? 'تم رفع التحديث بنجاح! سيعاد تشغيل الجهاز خلال ثوانٍ.'
                : 'فشل تحديث OTA'),
            backgroundColor: ok ? Colors.green : Colors.redAccent,
          ),
        );
      }
    } finally {
      await Future.delayed(const Duration(seconds: 5));
      if (mounted) {
        ref.read(statusPollingProvider.notifier).startPolling();
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.system_update_alt, color: Color(0xFF00F2FE), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'تحديث النظام الهوائي (OTA Firmware / LittleFS)',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                ),
              ),
            ],
          ),
          const SizedBox(height: 6),
          Text(
            'اختر ملف firmware.bin لتحديث نظام الجهاز أو littlefs.bin لتحديث واجهة الويب الداخلية تلقائياً.',
            style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
          ),
          const SizedBox(height: 14),
          OutlinedButton.icon(
            onPressed: _isUploading ? null : _pickBinFile,
            icon: const Icon(Icons.folder_open, color: Color(0xFF00F2FE)),
            label: Text(
              _selectedFileName ?? 'اختيار ملف التحديث (.bin)',
              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
              overflow: TextOverflow.ellipsis,
            ),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
              side: BorderSide(color: const Color(0xFF00F2FE).withOpacity(0.5)),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
            ),
          ),
          if (_isUploading || _statusText.isNotEmpty) ...[
            const SizedBox(height: 14),
            ClipRRect(
              borderRadius: BorderRadius.circular(8),
              child: LinearProgressIndicator(
                value: _progress,
                minHeight: 8,
                backgroundColor: Colors.white12,
                valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF00F2FE)),
              ),
            ),
            const SizedBox(height: 6),
            Text(
              _statusText,
              style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontSize: 12, fontWeight: FontWeight.bold),
              textAlign: TextAlign.center,
            ),
          ],
          const SizedBox(height: 14),
          ElevatedButton.icon(
            onPressed: (_isUploading || _selectedFilePath == null) ? null : _startOtaUpload,
            icon: _isUploading
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.black, strokeWidth: 2))
                : const Icon(Icons.cloud_upload),
            label: Text('بدء رفع وتثبيت التحديث', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00F2FE),
              foregroundColor: Colors.black87,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              elevation: 8,
            ),
          ),
        ],
      ),
    );
  }
}

class _ClockSettingsCard extends ConsumerStatefulWidget {
  const _ClockSettingsCard();

  @override
  ConsumerState<_ClockSettingsCard> createState() => _ClockSettingsCardState();
}

class _ClockSettingsCardState extends ConsumerState<_ClockSettingsCard> {
  bool _useNtp = true;
  final _timeController = TextEditingController(text: '12:00');
  final _dateController = TextEditingController(text: '2026-05-29');
  bool _isSaving = false;
  bool _isSyncingBrowser = false;
  bool _initialized = false;

  @override
  Widget build(BuildContext context) {
    final clockState = ref.watch(statusPollingProvider);
    
    if (!_initialized && clockState.currentClockTime != '--:--') {
      _timeController.text = clockState.currentClockTime;
      if (clockState.gregorianDate.isNotEmpty) {
        _dateController.text = clockState.gregorianDate;
      }
      _initialized = true;
    }

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.access_time_filled, color: Color(0xFF00FFCC), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'ضبط وقت وتاريخ النظام',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 20),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: ref.watch(timeFormatProvider),
            activeColor: const Color(0xFF00FFCC),
            title: Text('عرض الوقت بنظام 12 ساعة', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) {
              ref.read(timeFormatProvider.notifier).toggle(val);
            },
          ),
          const Divider(color: Colors.white10, height: 16),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _useNtp,
            activeColor: const Color(0xFF00FFCC),
            title: Text('تزامن تلقائي عبر الإنترنت (NTP)', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) {
              setState(() {
                _useNtp = val;
              });
            },
          ),
          if (!_useNtp) ...[
            const SizedBox(height: 12),
            TextField(
              controller: _timeController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'الوقت الحالي (HH:MM)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00FFCC), width: 1.5),
                ),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _dateController,
              style: const TextStyle(color: Colors.white),
              decoration: InputDecoration(
                labelText: 'التاريخ الحالي (YYYY-MM-DD)',
                labelStyle: GoogleFonts.cairo(color: Colors.white60, fontSize: 13),
                filled: true,
                fillColor: Colors.white.withOpacity(0.04),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(16)),
                enabledBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: BorderSide(color: Colors.white.withOpacity(0.08)),
                ),
                focusedBorder: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(16),
                  borderSide: const BorderSide(color: Color(0xFF00FFCC), width: 1.5),
                ),
              ),
            ),
          ],
          const SizedBox(height: 20),
          ElevatedButton.icon(
            onPressed: _isSaving ? null : () async {
              setState(() {
                _isSaving = true;
              });
              final success = await ref.read(apiServiceProvider).saveManualClock(
                _timeController.text.trim(),
                _dateController.text.trim(),
                _useNtp,
              );
              setState(() {
                _isSaving = false;
              });
              if (context.mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  SnackBar(
                    content: Text(success ? 'تم حفظ إعدادات الوقت بنجاح' : 'فشل حفظ إعدادات الوقت'),
                    backgroundColor: success ? Colors.green : Colors.redAccent,
                  ),
                );
              }
              ref.read(statusPollingProvider.notifier).pollStatus();
            },
            icon: _isSaving
                ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.black, strokeWidth: 2))
                : const Icon(Icons.save),
            label: Text('حفظ إعدادات الوقت', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFF00FFCC),
              foregroundColor: Colors.black87,
              padding: const EdgeInsets.symmetric(vertical: 14),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
              side: BorderSide(color: Colors.white.withOpacity(0.08)),
              elevation: 8,
            ),
          ),
          const SizedBox(height: 12),
          OutlinedButton.icon(
            onPressed: _isSyncingBrowser
                ? null
                : () async {
                    setState(() => _isSyncingBrowser = true);
                    final ok = await ref.read(apiServiceProvider).syncBrowserTime();
                    setState(() => _isSyncingBrowser = false);
                    if (context.mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        SnackBar(
                          content: Text(
                            ok
                                ? 'تمت مزامنة الوقت والتاريخ فوراً مع هذا الجهاز بنجاح'
                                : 'فشلت مزامنة الوقت والتاريخ مع الجهاز',
                          ),
                          backgroundColor: ok ? Colors.green : Colors.redAccent,
                        ),
                      );
                    }
                    ref.read(statusPollingProvider.notifier).pollStatus();
                  },
            icon: _isSyncingBrowser
                ? const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(color: Color(0xFF00FFCC), strokeWidth: 2),
                  )
                : const Icon(Icons.sync_rounded, color: Color(0xFF00FFCC)),
            label: Text(
              'مزامنة الوقت والتاريخ مع هذا الجهاز الآن',
              style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 13),
            ),
            style: OutlinedButton.styleFrom(
              padding: const EdgeInsets.symmetric(vertical: 13),
              side: const BorderSide(color: Color(0xFF00FFCC), width: 1.2),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
            ),
          ),
        ],
      ),
    );
  }
}

class _CsvCalendarManagerCard extends ConsumerStatefulWidget {
  const _CsvCalendarManagerCard();

  @override
  ConsumerState<_CsvCalendarManagerCard> createState() => _CsvCalendarManagerCardState();
}

class _CsvCalendarManagerCardState extends ConsumerState<_CsvCalendarManagerCard> {
  int _selectedYear = DateTime.now().year;
  Map<int, bool> _monthStatus = {};
  bool _csvEnabled = false;
  bool _calendarOnly = false;
  bool _calendarFallback = false;
  bool _isLoading = false;
  bool _isDownloading = false;
  String _downloadProgress = '';

  @override
  void initState() {
    super.initState();
    _loadStatus();
  }

  Future<void> _loadStatus() async {
    setState(() {
      _isLoading = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final status = await api.getCsvStatus(_selectedYear);

      if (status.isNotEmpty) {
        if (status.containsKey('enabled')) {
          _csvEnabled = status['enabled'] == true || status['enabled'] == 1 || status['enabled'] == '1';
        }
        if (status.containsKey('calendarOnly')) {
          _calendarOnly = status['calendarOnly'] == true || status['calendarOnly'] == 1 || status['calendarOnly'] == '1';
        }
        if (status.containsKey('calendarFallback')) {
          _calendarFallback = status['calendarFallback'] == true || status['calendarFallback'] == 1 || status['calendarFallback'] == '1';
        }
      }

      final monthsMap = <int, bool>{};
      for (int m = 1; m <= 12; m++) {
        monthsMap[m] = false;
      }
      if (status.containsKey('calendarMonths')) {
        final calMonths = status['calendarMonths'];
        if (calMonths is List) {
          for (var m in calMonths) {
            final monthNum = int.tryParse(m.toString());
            if (monthNum != null) {
              monthsMap[monthNum] = true;
            }
          }
        }
      } else if (status.containsKey('months')) {
        final months = status['months'];
        if (months is List) {
          for (var m in months) {
            final monthNum = int.tryParse(m.toString());
            if (monthNum != null) {
              monthsMap[monthNum] = true;
            }
          }
        }
      }
      if (mounted) {
        setState(() {
          _monthStatus = monthsMap;
        });
      }
    } catch (e) {
      debugPrint('Error loading CSV status: $e');
    } finally {
      if (mounted) {
        setState(() {
          _isLoading = false;
        });
      }
    }
  }

  Future<void> _downloadYear() async {
    setState(() {
      _isDownloading = true;
      _downloadProgress = 'جاري بدء تحميل الروزمانة...';
    });
    try {
      final api = ref.read(apiServiceProvider);
      final config = await api.getPrayerConfig();
      final country = config['country'] ?? 'Saudi Arabia';
      final city = config['city'] ?? 'Makkah';
      int method = 2; // UmmAlQura
      final methodStr = config['method']?.toString() ?? '';
      if (methodStr == 'Egypt') method = 0;
      if (methodStr == 'MWL') method = 1;

      for (int m = 1; m <= 12; m++) {
        if (!mounted) break;
        setState(() {
          _downloadProgress = 'تحميل الشهر $m من 12...';
        });

        final startRes = await api.downloadCalendarMonth(_selectedYear, m, country, city, method, force: true);
        if (startRes['ok'] != true) {
          throw 'فشل بدء تحميل الشهر $m';
        }

        int checks = 0;
        while (checks < 30) {
          await Future.delayed(const Duration(milliseconds: 1000));
          final dlStatus = await api.getCalendarDownloadStatus();
          final isBusy = dlStatus['busy'] == true || dlStatus['busy'] == 1 || dlStatus['busy'] == '1';
          if (!isBusy) {
            break;
          }
          checks++;
        }
      }

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('تم تحميل تقويم السنة كاملة بنجاح!'), backgroundColor: Colors.green),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('حدث خطأ أثناء التحميل: $e'), backgroundColor: Colors.redAccent),
        );
      }
    } finally {
      setState(() {
        _isDownloading = false;
        _downloadProgress = '';
      });
      _loadStatus();
    }
  }

  Future<void> _deleteYear() async {
    final confirm = await showDialog<bool>(
      context: context,
      builder: (ctx) => Directionality(
        textDirection: TextDirection.rtl,
        child: AlertDialog(
          backgroundColor: const Color(0xFF1C132E),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
          title: Text('تأكيد الحذف', style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold)),
          content: Text('هل أنت متأكد من رغبتك في حذف ملفات الروزمانة لسنة $_selectedYear؟', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF))),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
            ),
            ElevatedButton(
              onPressed: () => Navigator.pop(ctx, true),
              style: ElevatedButton.styleFrom(backgroundColor: Colors.redAccent),
              child: Text('حذف', style: GoogleFonts.cairo(color: Colors.white)),
            ),
          ],
        ),
      ),
    );

    if (confirm != true) return;

    setState(() {
      _isLoading = true;
    });
    try {
      final success = await ref.read(apiServiceProvider).deleteYearCalendar(_selectedYear);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(success ? 'تم حذف ملفات السنة بنجاح' : 'فشل حذف الملفات'),
            backgroundColor: success ? Colors.green : Colors.redAccent,
          ),
        );
      }
    } catch (e) {
      debugPrint('Error deleting year: $e');
    } finally {
      _loadStatus();
    }
  }

  Future<void> _uploadCustomCsv(int month) async {
    try {
      final result = await FilePicker.platform.pickFiles(
        type: FileType.custom,
        allowedExtensions: ['csv'],
      );

      if (result == null || result.files.single.path == null) return;

      final file = File(result.files.single.path!);
      final bytes = await file.readAsBytes();
      final filename = result.files.single.name;

      setState(() {
        _isLoading = true;
      });

      final success = await ref.read(apiServiceProvider).uploadCsvMonth(month, bytes, filename);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(success ? 'تم رفع ملف CSV للشهر $month بنجاح' : 'فشل رفع ملف CSV'),
            backgroundColor: success ? Colors.green : Colors.redAccent,
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('حدث خطأ أثناء الرفع: $e'), backgroundColor: Colors.redAccent),
        );
      }
    } finally {
      _loadStatus();
    }
  }

  Future<void> _inspectMonth(int month) async {
    setState(() {
      _isLoading = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final res = await api.getCalendarMonth(_selectedYear, month);
      setState(() {
        _isLoading = false;
      });

      if (res['ok'] != true || res['csv'] == null) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('فشل تحميل محتويات التقويم لهذا الشهر'), backgroundColor: Colors.redAccent),
          );
        }
        return;
      }

      final csvContent = res['csv'] as String;
      final textController = TextEditingController(text: csvContent);

      if (!mounted) return;

      await showDialog(
        context: context,
        builder: (ctx) => Directionality(
          textDirection: TextDirection.rtl,
          child: AlertDialog(
            backgroundColor: const Color(0xFF1C132E),
            shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20)),
            title: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(
                  'تفاصيل تقويم الشهر $month / $_selectedYear',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 16),
                ),
                TextButton.icon(
                  onPressed: () => _uploadCustomCsv(month).then((_) => Navigator.pop(ctx)),
                  icon: const Icon(Icons.upload, size: 16, color: Color(0xFF00F2FE)),
                  label: Text('رفع CSV جديد', style: GoogleFonts.cairo(color: const Color(0xFF00F2FE), fontSize: 12)),
                ),
              ],
            ),
            content: SizedBox(
              width: MediaQuery.of(context).size.width * 0.85,
              height: MediaQuery.of(context).size.height * 0.6,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    'محتوى ملف الروزمانة (CSV):',
                    style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
                  ),
                  const SizedBox(height: 8),
                  Expanded(
                    child: TextField(
                      controller: textController,
                      maxLines: null,
                      expands: true,
                      style: GoogleFonts.sourceCodePro(color: Colors.greenAccent, fontSize: 12),
                      decoration: InputDecoration(
                        filled: true,
                        fillColor: Colors.black26,
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                    ),
                  ),
                ],
              ),
            ),
            actions: [
              TextButton(
                onPressed: () => Navigator.pop(ctx),
                child: Text('إلغاء', style: GoogleFonts.cairo(color: Colors.grey)),
              ),
              ElevatedButton.icon(
                onPressed: () async {
                  final newCsv = textController.text;
                  Navigator.pop(ctx);
                  setState(() {
                    _isLoading = true;
                  });
                  final saveSuccess = await api.saveCalendarMonth(_selectedYear, month, newCsv);
                  if (mounted) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      SnackBar(
                        content: Text(saveSuccess ? 'تم حفظ التعديلات بنجاح' : 'فشل حفظ التعديلات'),
                        backgroundColor: saveSuccess ? Colors.green : Colors.redAccent,
                      ),
                    );
                  }
                  _loadStatus();
                },
                icon: const Icon(Icons.save, size: 16),
                label: Text('حفظ التغييرات', style: GoogleFonts.cairo(fontWeight: FontWeight.bold)),
                style: ElevatedButton.styleFrom(backgroundColor: const Color(0xFF00F2FE)),
              ),
            ],
          ),
        ),
      );
    } catch (e) {
      setState(() {
        _isLoading = false;
      });
      debugPrint('Error inspecting month: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.calendar_view_month, color: Color(0xFF00F2FE), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'إدارة الروزمانة والتقاويم (CSV)',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const Divider(color: Colors.white10, height: 24),

          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _csvEnabled,
            activeColor: const Color(0xFF00F2FE),
            title: Text('تفعيل استخدام الروزمانة', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) async {
              setState(() { _csvEnabled = val; });
              final success = await ref.read(apiServiceProvider).toggleCsvMode(val);
              if (!success) {
                if (mounted) {
                  setState(() { _csvEnabled = !val; });
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text('فشل تغيير حالة استخدام الروزمانة', style: GoogleFonts.cairo()),
                      backgroundColor: Colors.redAccent,
                    ),
                  );
                }
              } else {
                ref.read(statusPollingProvider.notifier).pollStatus();
              }
            },
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _calendarOnly,
            activeColor: const Color(0xFF00F2FE),
            title: Text('الاعتماد على الروزمانة فقط (دون حساب)', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) async {
              setState(() { _calendarOnly = val; });
              final success = await ref.read(apiServiceProvider).toggleCalendarOnly(val);
              if (!success) {
                if (mounted) {
                  setState(() { _calendarOnly = !val; });
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text('فشل تغيير إعداد الاعتماد على الروزمانة فقط', style: GoogleFonts.cairo()),
                      backgroundColor: Colors.redAccent,
                    ),
                  );
                }
              } else {
                ref.read(statusPollingProvider.notifier).pollStatus();
              }
            },
          ),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            value: _calendarFallback,
            activeColor: const Color(0xFF00F2FE),
            title: Text('استخدام الروزمانة كاحتياط عند انقطاع الإنترنت', style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13)),
            onChanged: (val) async {
              setState(() { _calendarFallback = val; });
              final success = await ref.read(apiServiceProvider).toggleCalendarFallback(val);
              if (!success) {
                if (mounted) {
                  setState(() { _calendarFallback = !val; });
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text('فشل تغيير إعداد الاحتياطي عند انقطاع الإنترنت', style: GoogleFonts.cairo()),
                      backgroundColor: Colors.redAccent,
                    ),
                  );
                }
              } else {
                ref.read(statusPollingProvider.notifier).pollStatus();
              }
            },
          ),
          const Divider(color: Colors.white10, height: 24),

          Row(
            children: [
              Text('السنة:', style: GoogleFonts.cairo(color: Colors.white, fontSize: 13)),
              const SizedBox(width: 12),
              DropdownButton<int>(
                dropdownColor: const Color(0xFF1C132E),
                value: _selectedYear,
                style: const TextStyle(color: Colors.white, fontSize: 14, fontWeight: FontWeight.bold),
                items: [2024, 2025, 2026, 2027, 2028, 2029].map((y) {
                  return DropdownMenuItem<int>(value: y, child: Text('$y'));
                }).toList(),
                onChanged: (val) {
                  if (val != null) {
                    setState(() { _selectedYear = val; });
                    _loadStatus();
                  }
                },
              ),
            ],
          ),
          const SizedBox(height: 12),

          if (_isDownloading) ...[
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(color: Colors.white.withOpacity(0.05), borderRadius: BorderRadius.circular(12)),
              child: Column(
                children: [
                  const SizedBox(height: 8, child: LinearProgressIndicator(color: Color(0xFF00F2FE))),
                  const SizedBox(height: 8),
                  Text(_downloadProgress, style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12)),
                ],
              ),
            ),
            const SizedBox(height: 16),
          ] else ...[
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isLoading ? null : _downloadYear,
                    icon: const Icon(Icons.download, size: 16),
                    label: FittedBox(
                      fit: BoxFit.scaleDown,
                      child: Text('تحميل رزنامة السنة إلى SD', style: GoogleFonts.cairo(fontSize: 11, fontWeight: FontWeight.bold)),
                    ),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF00F2FE),
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 6),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isLoading ? null : _deleteYear,
                    icon: const Icon(Icons.delete, size: 16),
                    label: FittedBox(
                      fit: BoxFit.scaleDown,
                      child: Text('حذف رزنامة السنة', style: GoogleFonts.cairo(fontSize: 11, fontWeight: FontWeight.bold)),
                    ),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: Colors.redAccent,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 6),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 16),
          ],

          Text(
            'حالة تقاويم الأشهر لـ $_selectedYear:',
            style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12, fontWeight: FontWeight.bold),
          ),
          const SizedBox(height: 10),
          _isLoading
              ? const Center(child: Padding(padding: EdgeInsets.all(20), child: CircularProgressIndicator(color: Color(0xFF00F2FE))))
              : GridView.builder(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                    maxCrossAxisExtent: 110,
                    crossAxisSpacing: 8,
                    mainAxisSpacing: 8,
                    childAspectRatio: 1.1,
                  ),
                  itemCount: 12,
                  itemBuilder: (context, index) {
                    final month = index + 1;
                    final available = _monthStatus[month] ?? false;
                    return InkWell(
                      onTap: () {
                        if (available) {
                          _inspectMonth(month);
                        } else {
                          _uploadCustomCsv(month);
                        }
                      },
                      child: Container(
                        decoration: BoxDecoration(
                          color: available ? Colors.green.withOpacity(0.12) : Colors.redAccent.withOpacity(0.08),
                          borderRadius: BorderRadius.circular(12),
                          border: Border.all(
                            color: available ? Colors.green.withOpacity(0.4) : Colors.redAccent.withOpacity(0.3),
                            width: 1.2,
                          ),
                        ),
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            Text(
                              '$month',
                              style: GoogleFonts.orbitron(
                                color: available ? Colors.greenAccent : Colors.redAccent,
                                fontSize: 16,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                            const SizedBox(height: 2),
                            Text(
                              available ? 'موجود' : 'مفقود',
                              style: GoogleFonts.cairo(
                                color: available ? Colors.greenAccent : Colors.redAccent,
                                fontSize: 10,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                      ),
                    );
                  },
                ),
        ],
      ),
    );
  }
}

class _DailyOffsetCard extends ConsumerStatefulWidget {
  const _DailyOffsetCard();

  @override
  ConsumerState<_DailyOffsetCard> createState() => _DailyOffsetCardState();
}

class _DailyOffsetCardState extends ConsumerState<_DailyOffsetCard> {
  DateTime _selectedDate = DateTime.now();
  final _fajrController = TextEditingController(text: '0');
  final _dhuhrController = TextEditingController(text: '0');
  final _asrController = TextEditingController(text: '0');
  final _maghribController = TextEditingController(text: '0');
  final _ishaController = TextEditingController(text: '0');
  
  bool _isLoading = false;
  bool _isSaving = false;
  bool _exists = false;

  @override
  void initState() {
    super.initState();
    _loadDailyOffsets();
  }

  String get _formattedDate => 
      "${_selectedDate.year}-${_selectedDate.month.toString().padLeft(2, '0')}-${_selectedDate.day.toString().padLeft(2, '0')}";

  Future<void> _loadDailyOffsets() async {
    setState(() {
      _isLoading = true;
    });
    try {
      final res = await ref.read(apiServiceProvider).getDailyOffset(_formattedDate);
      if (res['ok'] == true) {
        setState(() {
          _exists = res['exists'] ?? false;
          _fajrController.text = (res['fajr'] ?? 0).toString();
          _dhuhrController.text = (res['dhuhr'] ?? 0).toString();
          _asrController.text = (res['asr'] ?? 0).toString();
          _maghribController.text = (res['maghrib'] ?? 0).toString();
          _ishaController.text = (res['isha'] ?? 0).toString();
        });
      }
    } catch (e) {
      debugPrint('Error loading daily offsets: $e');
    } finally {
      setState(() {
        _isLoading = false;
      });
    }
  }

  Future<void> _saveOffsets() async {
    setState(() {
      _isSaving = true;
    });
    try {
      final success = await ref.read(apiServiceProvider).saveDailyOffset(
        _formattedDate,
        int.tryParse(_fajrController.text) ?? 0,
        int.tryParse(_dhuhrController.text) ?? 0,
        int.tryParse(_asrController.text) ?? 0,
        int.tryParse(_maghribController.text) ?? 0,
        int.tryParse(_ishaController.text) ?? 0,
      );
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(success ? 'تم حفظ تعديلات اليوم بنجاح' : 'فشل حفظ التعديلات'),
            backgroundColor: success ? Colors.green : Colors.redAccent,
          ),
        );
      }
      if (success) {
        _loadDailyOffsets();
        ref.read(statusPollingProvider.notifier).pollStatus();
      }
    } catch (e) {
      debugPrint('Error saving offsets: $e');
    } finally {
      setState(() {
        _isSaving = false;
      });
    }
  }

  Future<void> _deleteOffsets() async {
    setState(() {
      _isSaving = true;
    });
    try {
      final success = await ref.read(apiServiceProvider).deleteDailyOffset(_formattedDate);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(success ? 'تم حذف تعديلات اليوم بنجاح' : 'فشل حذف التعديلات'),
            backgroundColor: success ? Colors.green : Colors.redAccent,
          ),
        );
      }
      if (success) {
        _loadDailyOffsets();
        ref.read(statusPollingProvider.notifier).pollStatus();
      }
    } catch (e) {
      debugPrint('Error deleting offsets: $e');
    } finally {
      setState(() {
        _isSaving = false;
      });
    }
  }

  Future<void> _selectDate(BuildContext context) async {
    final DateTime? picked = await showDatePicker(
      context: context,
      initialDate: _selectedDate,
      firstDate: DateTime(2020),
      lastDate: DateTime(2035),
      locale: const Locale('ar', 'AE'),
      builder: (context, child) {
        return Theme(
          data: Theme.of(context).copyWith(
            colorScheme: const ColorScheme.dark(
              primary: Color(0xFF00F2FE),
              onPrimary: Colors.black,
              surface: Color(0xFF1C132E),
              onSurface: Colors.white,
            ),
            dialogBackgroundColor: const Color(0xFF16132C),
          ),
          child: child!,
        );
      },
    );
    if (picked != null && picked != _selectedDate) {
      setState(() {
        _selectedDate = picked;
      });
      _loadDailyOffsets();
    }
  }

  Widget _buildOffsetInput(String label, TextEditingController controller) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 12.0),
      child: Row(
        children: [
          SizedBox(
            width: 80,
            child: Text(
              label,
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 13, fontWeight: FontWeight.bold),
            ),
          ),
          Expanded(
            child: SizedBox(
              height: 44,
              child: TextField(
                controller: controller,
                keyboardType: TextInputType.number,
                style: const TextStyle(color: Colors.white, fontSize: 14),
                decoration: InputDecoration(
                  hintText: '0',
                  hintStyle: const TextStyle(color: Colors.white24),
                  suffixText: 'دقيقة',
                  suffixStyle: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
                  filled: true,
                  fillColor: Colors.black26,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 16),
                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.edit_calendar, color: Color(0xFF00F2FE), size: 20),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'تصحيح مواقيت يوم محدد',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 14),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const Divider(color: Colors.white10, height: 24),
          Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Text(
                'التاريخ المحدد:',
                style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
              ),
              TextButton.icon(
                onPressed: () => _selectDate(context),
                icon: const Icon(Icons.calendar_today, size: 14, color: Color(0xFF00F2FE)),
                label: Text(
                  _formattedDate,
                  style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontSize: 13, fontWeight: FontWeight.bold),
                ),
                style: TextButton.styleFrom(
                  backgroundColor: const Color(0xFF00F2FE).withOpacity(0.08),
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                  shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                ),
              ),
            ],
          ),
          const SizedBox(height: 16),
          if (_isLoading)
            const SizedBox(
              height: 180,
              child: Center(
                child: CircularProgressIndicator(color: Color(0xFF00F2FE)),
              ),
            )
          else ...[
            _buildOffsetInput('الفجر', _fajrController),
            _buildOffsetInput('الظهر', _dhuhrController),
            _buildOffsetInput('العصر', _asrController),
            _buildOffsetInput('المغرب', _maghribController),
            _buildOffsetInput('العشاء', _ishaController),
            const SizedBox(height: 16),
            Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    onPressed: _isSaving ? null : _saveOffsets,
                    icon: _isSaving
                        ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                        : const Icon(Icons.save, size: 16),
                    label: Text('حفظ التعديل', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF00F2FE),
                      foregroundColor: Colors.black,
                      padding: const EdgeInsets.symmetric(vertical: 12),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                ),
                if (_exists) ...[
                  const SizedBox(width: 12),
                  Expanded(
                    child: ElevatedButton.icon(
                      onPressed: _isSaving ? null : _deleteOffsets,
                      icon: const Icon(Icons.delete_forever, size: 16),
                      label: Text('حذف التعديل', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.redAccent.withOpacity(0.15),
                        foregroundColor: Colors.redAccent,
                        side: BorderSide(color: Colors.redAccent.withOpacity(0.3)),
                        padding: const EdgeInsets.symmetric(vertical: 12),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                      ),
                    ),
                  ),
                ],
              ],
            ),
          ],
        ],
      ),
    );
  }
}

class _SystemSettingsCard extends ConsumerStatefulWidget {
  const _SystemSettingsCard();

  @override
  ConsumerState<_SystemSettingsCard> createState() => _SystemSettingsCardState();
}

class _SystemSettingsCardState extends ConsumerState<_SystemSettingsCard> {
  bool _isLoading = true;
  bool _isSaving = false;
  
  bool _startupEnabled = false;
  String _startupFile = '';
  final _timeoutController = TextEditingController(text: '10');

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    setState(() {
      _isLoading = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final startup = await api.getStartupStatus();
      final timeout = await api.getSessionTimeout();
      
      setState(() {
        _startupEnabled = startup['enabled'] ?? false;
        _startupFile = startup['file'] ?? '';
        _timeoutController.text = timeout.toString();
        _isLoading = false;
      });
    } catch (e) {
      debugPrint('Error loading system settings: $e');
      setState(() {
        _isLoading = false;
      });
    }
  }

  Future<void> _saveSettings() async {
    setState(() {
      _isSaving = true;
    });
    try {
      final api = ref.read(apiServiceProvider);
      final success1 = await api.saveStartupStatus(_startupEnabled, _startupFile);
      final timeoutVal = int.tryParse(_timeoutController.text) ?? 10;
      final success2 = await api.saveSessionTimeout(timeoutVal);
      
      if (mounted) {
        final success = success1 && success2;
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(success ? 'تم حفظ إعدادات النظام بنجاح' : 'حدث خطأ أثناء حفظ بعض الإعدادات'),
            backgroundColor: success ? Colors.green : Colors.redAccent,
          ),
        );
      }
    } catch (e) {
      debugPrint('Error saving system settings: $e');
    } finally {
      setState(() {
        _isSaving = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    final filesState = ref.watch(filesProvider);
    final List<String> audioFiles = [];
    filesState.whenData((data) {
      final list = data['files'] as List? ?? [];
      for (var f in list) {
        final name = f['name'] as String? ?? '';
        final isDir = f['isDirectory'] as bool? ?? false;
        if (!isDir && (name.endsWith('.mp3') || name.endsWith('.wav'))) {
          audioFiles.add(name);
        }
      }
    });

    return Container(
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        color: const Color(0x7316132C),
        borderRadius: BorderRadius.circular(24),
        border: Border.all(color: Colors.white.withOpacity(0.08)),
        boxShadow: [
          BoxShadow(
            color: Colors.black.withOpacity(0.05),
            blurRadius: 15,
            offset: const Offset(0, 8),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              const Icon(Icons.settings_system_daydream, color: Color(0xFFBF5AF2), size: 22),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  'إعدادات النظام العامة',
                  style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 15),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const Divider(color: Colors.white10, height: 24),
          if (_isLoading)
            const SizedBox(
              height: 150,
              child: Center(
                child: CircularProgressIndicator(color: Color(0xFF00F2FE)),
              ),
            )
          else ...[
            SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: Text(
                'تفعيل نغمة بدء تشغيل الجهاز',
                style: GoogleFonts.cairo(color: Colors.white, fontSize: 13),
              ),
              subtitle: Text(
                'تشغيل ملف صوتي عند إقلاع الجهاز',
                style: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
              ),
              value: _startupEnabled,
              activeColor: const Color(0xFFBF5AF2),
              onChanged: (val) {
                setState(() {
                  _startupEnabled = val;
                });
              },
            ),
            if (_startupEnabled) ...[
              const SizedBox(height: 8),
              Text(
                'اختر ملف بدء التشغيل:',
                style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
              ),
              const SizedBox(height: 8),
              DropdownButtonFormField<String>(
                isExpanded: true,
                dropdownColor: const Color(0xFF1C132E),
                style: const TextStyle(color: Colors.white, fontSize: 13),
                value: audioFiles.contains(_startupFile) ? _startupFile : (audioFiles.isNotEmpty ? audioFiles.first : null),
                decoration: InputDecoration(
                  filled: true,
                  fillColor: Colors.black26,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                ),
                items: audioFiles.map((f) => DropdownMenuItem<String>(value: f, child: Text(f, overflow: TextOverflow.ellipsis, maxLines: 1))).toList(),
                onChanged: (val) {
                  setState(() {
                    _startupFile = val ?? '';
                  });
                },
              ),
              const SizedBox(height: 16),
            ],
            Text(
              'مدة الجلسة التلقائية (بالدقائق):',
              style: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 12),
            ),
            const SizedBox(height: 8),
            SizedBox(
              height: 44,
              child: TextField(
                controller: _timeoutController,
                keyboardType: TextInputType.number,
                style: const TextStyle(color: Colors.white, fontSize: 14),
                decoration: InputDecoration(
                  suffixText: 'دقيقة',
                  suffixStyle: GoogleFonts.cairo(color: Colors.white38, fontSize: 11),
                  filled: true,
                  fillColor: Colors.black26,
                  contentPadding: const EdgeInsets.symmetric(horizontal: 16),
                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                ),
              ),
            ),
            const SizedBox(height: 20),
            ElevatedButton.icon(
              onPressed: _isSaving ? null : _saveSettings,
              icon: _isSaving
                  ? const SizedBox(width: 14, height: 14, child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2))
                  : const Icon(Icons.save, size: 16),
              label: Text('حفظ إعدادات النظام', style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 12)),
              style: ElevatedButton.styleFrom(
                backgroundColor: const Color(0xFFBF5AF2),
                foregroundColor: Colors.white,
                padding: const EdgeInsets.symmetric(vertical: 12),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _QuranRadioTab extends ConsumerStatefulWidget {
  final void Function(String url, String name)? onSchedule;
  const _QuranRadioTab({this.onSchedule});

  @override
  ConsumerState<_QuranRadioTab> createState() => _QuranRadioTabState();
}

class _QuranRadioTabState extends ConsumerState<_QuranRadioTab> {
  final _dio = Dio();
  List<dynamic> _allRadios = [];
  List<dynamic> _filteredRadios = [];
  bool _isLoading = false;
  String _statusText = 'جاري تحميل قائمة الإذاعات...';
  String _searchQuery = '';
  String _selectedCountry = '';
  String? _currentPlayingRadioTitle;

  final _searchController = TextEditingController();
  double? _localRadioVolume;
  bool _isDraggingRadioVolume = false;
  Timer? _radioVolumeDebounceTimer;

  final List<Map<String, dynamic>> _offlineRadios = const [
    { "name": "إذاعة القرآن الكريم من القاهرة", "url": "http://stream.radiojar.com/8s5u5tpdtwzuv" },
    { "name": "إذاعة القرآن الكريم - السعودية", "url": "https://stream.radiojar.com/0tpy1h0kxtzuv" },
    { "name": "الإذاعة العامة - تلاوات متنوعة (Mp3Quran)", "url": "https://backup.qurango.net/radio/mix" },
    { "name": "إذاعة تكبيرات العيد", "url": "https://backup.qurango.net/radio/eid" },
    { "name": "إذاعة الرقية الشرعية", "url": "https://backup.qurango.net/radio/roqiah" },
    { "name": "إذاعة تفسير القرآن الكريم", "url": "https://backup.qurango.net/radio/tafseer" },
    { "name": "آيات السكينة (تلاوات هادئة)", "url": "https://backup.qurango.net/radio/sakeenah" },
    { "name": "أذكار الصباح (بث مباشر)", "url": "https://backup.qurango.net/radio/athkar_sabah" },
    { "name": "أذكار المساء (بث مباشر)", "url": "https://backup.qurango.net/radio/athkar_masa" },
    { "name": "إذاعة مشاري العفاسي", "url": "https://backup.qurango.net/radio/mishary_alafasi" },
    { "name": "إذاعة عبد الرحمن السديس", "url": "https://backup.qurango.net/radio/abdulrahman_alsudaes" },
    { "name": "إذاعة ماهر المعيقلي", "url": "https://backup.qurango.net/radio/maher" },
    { "name": "إذاعة عبد الباسط عبد الصمد (المصحف المجود)", "url": "https://backup.qurango.net/radio/abdulbasit_abdulsamad_mojawwad" }
  ];

  @override
  void initState() {
    super.initState();
    _fetchRadios();
  }

  Future<void> _fetchRadios({bool force = false}) async {
    setState(() {
      _isLoading = true;
      _statusText = 'جاري الاتصال بالسيرفر وجلب الإذاعات...';
    });

    try {
      final response = await _dio.get('https://mp3quran.net/api/v3/radios?language=ar');
      if (response.statusCode == 200) {
        final data = response.data;
        if (data != null && data['radios'] is List) {
          final fetched = data['radios'] as List;
          final cairoRadio = {
            "id": 99999,
            "name": "إذاعة القرآن الكريم من القاهرة",
            "url": "http://stream.radiojar.com/8s5u5tpdtwzuv"
          };
          setState(() {
            _allRadios = [cairoRadio, ...fetched];
            _statusText = 'تم تحميل ${_allRadios.length} إذاعة بنجاح.';
            _isLoading = false;
          });
          _filterRadios();
          return;
        }
      }
      throw Exception('Format error');
    } catch (e) {
      debugPrint('Failed to fetch radios online: $e');
      setState(() {
        _allRadios = _offlineRadios;
        _statusText = 'فشل الاتصال بالإنترنت. تم تحميل القائمة الافتراضية (${_allRadios.length} إذاعة أوفلاين).';
        _isLoading = false;
      });
      _filterRadios();
    }
  }

  void _filterRadios() {
    final query = _searchQuery.toLowerCase().trim();
    final country = _selectedCountry;

    final Map<String, List<String>> countryKeywords = {
      "السعودية": [
        "السعودية", "saudi", "المملكة", "الرياض", "مكة", "المدينة", "حرم", "saudia",
        "السديس", "sudaes", "الشريم", "shuraim", "المعيقلي", "muaiqly", "meaqli",
        "الدوسري", "dossari", "القطامي", "qatami", "الحذيفي", "huthaify", "الغامدي", "ghamdi",
        "البدير", "budair", "الجهني", "juhany", "العجمي", "ajmy"
      ],
      "مصر": [
        "مصر", "egypt", "القاهرة", "cairo", "طنطا", "المنصورة",
        "عبد الباسط", "abdulsamad", "abdul_basit", "الحصري", "hussary", "hosary",
        "المنشاوي", "minshawi", "مصطفى إسماعيل", "mustafa ismail", "البنا", "banna",
        "رفعت", "rifat", "rifaat"
      ],
      "الكويت": ["الكويت", "kuwait", "العفاسي", "alafasi"],
      "الإمارات": ["الإمارات", "دبي", "أبوظبي", "dubai", "uae", "sharjah", "الشارقة"],
      "فلسطين": ["فلسطين", "القدس", "palestine", "gaza", "غزة"],
      "الجزائر": ["الجزائر", "algeria", "algerie"],
      "المغرب": ["المغرب", "morocco", "maroc", "الرباط", "الدار البيضاء"],
      "الأردن": ["الأردن", "jordan", "عمان"],
      "اليمن": ["اليمن", "yemen", "صنعاء", "عباد", "abbad"],
      "عمان": ["عمان", "oman", "مسقط"],
      "البحرين": ["البحرين", "bahrain", "المنامة"],
      "قطر": ["قطر", "qatar", "الدوحة"],
      "ليبيا": ["ليبيا", "libya", "طرابلس"]
    };

    setState(() {
      _filteredRadios = _allRadios.where((radio) {
        final name = (radio['name'] as String? ?? '').toLowerCase();
        final url = (radio['url'] as String? ?? '').toLowerCase();
        final matchesQuery = name.contains(query) || url.contains(query);

        bool matchesCountry = true;
        if (country.isNotEmpty) {
          final keywords = countryKeywords[country] ?? [country];
          matchesCountry = keywords.any((kw) => name.contains(kw.toLowerCase()));
        }

        return matchesQuery && matchesCountry;
      }).toList();
    });
  }

  Future<void> _playRadio(String url, String name) async {
    setState(() {
      _currentPlayingRadioTitle = name;
    });
    final success = await ref.read(filesProvider.notifier).playFile(url);
    if (mounted) {
      ref.read(statusPollingProvider.notifier).pollStatus();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(success ? 'جاري تشغيل: $name' : 'فشل تشغيل البث المباشر'),
          backgroundColor: success ? Colors.green : Colors.redAccent,
        ),
      );
    }
  }

  @override
  void dispose() {
    _radioVolumeDebounceTimer?.cancel();
    _searchController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final status = ref.watch(statusPollingProvider);
    final currentVolDisplay = ((_isDraggingRadioVolume ? _localRadioVolume : null) ?? status.volume.toDouble()).round().clamp(0, 30);

    return Padding(
      padding: const EdgeInsets.all(16.0),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Quick Player & Volume Control Card for Quran Radio
          Container(
            margin: const EdgeInsets.only(bottom: 12),
            padding: const EdgeInsets.all(14),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(
                color: status.isPlaying ? const Color(0xFF00F2FE).withOpacity(0.4) : Colors.white.withOpacity(0.08),
                width: status.isPlaying ? 1.5 : 1.0,
              ),
              boxShadow: [
                BoxShadow(
                  color: status.isPlaying ? const Color(0xFF00F2FE).withOpacity(0.08) : Colors.black.withOpacity(0.05),
                  blurRadius: 15,
                  offset: const Offset(0, 6),
                ),
              ],
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Header with state and title
                Row(
                  children: [
                    Icon(
                      status.isPlaying ? Icons.radio : Icons.radio_outlined,
                      color: status.isPlaying ? const Color(0xFF00F2FE) : const Color(0xFFADCCFF),
                      size: 20,
                    ),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            _currentPlayingRadioTitle ?? (status.currentFile.isNotEmpty ? status.currentFile : 'المشغل السريع للراديو'),
                            style: GoogleFonts.cairo(
                              fontSize: 13,
                              fontWeight: FontWeight.bold,
                              color: Colors.white,
                            ),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                          ),
                          Text(
                            status.isPlaying ? 'قيد التشغيل المباشر الآن' : 'البث متوقف',
                            style: GoogleFonts.cairo(
                              fontSize: 10,
                              color: status.isPlaying ? const Color(0xFF00FFCC) : Colors.white38,
                            ),
                          ),
                        ],
                      ),
                    ),
                    // Stop button
                    IconButton(
                      icon: const Icon(Icons.stop_circle_rounded, color: Colors.redAccent, size: 26),
                      tooltip: 'إيقاف التشغيل',
                      onPressed: () async {
                        await ref.read(apiServiceProvider).stopAudio();
                        ref.read(quickPlayerProvider.notifier).stop();
                        ref.read(statusPollingProvider.notifier).pollStatus();
                      },
                    ),
                    // Play/Pause button
                    IconButton(
                      icon: Icon(
                        status.isPlaying ? Icons.pause_circle_filled_rounded : Icons.play_circle_filled_rounded,
                        color: const Color(0xFF00F2FE),
                        size: 28,
                      ),
                      tooltip: status.isPlaying ? 'إيقاف مؤقت' : 'تشغيل',
                      onPressed: () async {
                        if (status.isPlaying) {
                          await ref.read(apiServiceProvider).stopAudio();
                          ref.read(quickPlayerProvider.notifier).stop();
                        } else if (_currentPlayingRadioTitle != null && _allRadios.isNotEmpty) {
                          final match = _allRadios.firstWhere(
                            (r) => r['name'] == _currentPlayingRadioTitle,
                            orElse: () => null,
                          );
                          if (match != null) {
                            await _playRadio(match['url'] ?? '', match['name'] ?? '');
                          }
                        }
                        ref.read(statusPollingProvider.notifier).pollStatus();
                      },
                    ),
                  ],
                ),
                const Divider(color: Colors.white10, height: 16),
                // Smooth Volume Slider Row
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Row(
                      children: [
                        const Icon(Icons.volume_up_rounded, color: Color(0xFF00F2FE), size: 18),
                        const SizedBox(width: 6),
                        Text(
                          'مستوى الصوت',
                          style: GoogleFonts.cairo(fontSize: 12, fontWeight: FontWeight.bold, color: Colors.white70),
                        ),
                      ],
                    ),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                      decoration: BoxDecoration(
                        color: const Color(0xFF00F2FE).withOpacity(0.15),
                        borderRadius: BorderRadius.circular(6),
                        border: Border.all(color: const Color(0xFF00F2FE).withOpacity(0.3)),
                      ),
                      child: Text(
                        '$currentVolDisplay',
                        style: GoogleFonts.orbitron(color: const Color(0xFF00F2FE), fontSize: 11, fontWeight: FontWeight.bold),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 4),
                Row(
                  children: [
                    IconButton(
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(),
                      icon: const Icon(Icons.remove_circle_outline_rounded, color: Colors.white60, size: 22),
                      onPressed: () async {
                        final current = (_localRadioVolume ?? status.volume.toDouble()).round();
                        if (current > 0) {
                          final newVol = current - 1;
                          setState(() {
                            _localRadioVolume = newVol.toDouble();
                            _isDraggingRadioVolume = true;
                          });
                          _radioVolumeDebounceTimer?.cancel();
                          await ref.read(apiServiceProvider).setVolume(newVol);
                          ref.read(statusPollingProvider.notifier).pollStatus();
                          Future.delayed(const Duration(milliseconds: 800), () {
                            if (mounted) {
                              setState(() {
                                _isDraggingRadioVolume = false;
                                _localRadioVolume = null;
                              });
                            }
                          });
                        }
                      },
                    ),
                    Expanded(
                      child: SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: const Color(0xFF00F2FE),
                          inactiveTrackColor: Colors.white.withOpacity(0.1),
                          thumbColor: const Color(0xFF00F2FE),
                          overlayColor: const Color(0xFF00F2FE).withOpacity(0.2),
                          trackHeight: 5,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 7),
                          overlayShape: const RoundSliderOverlayShape(overlayRadius: 16),
                        ),
                        child: Slider(
                          value: ((_isDraggingRadioVolume ? _localRadioVolume : null) ?? status.volume.toDouble()).clamp(0.0, 30.0),
                          min: 0,
                          max: 30,
                          onChanged: (value) {
                            setState(() {
                              _isDraggingRadioVolume = true;
                              _localRadioVolume = value;
                            });
                            _radioVolumeDebounceTimer?.cancel();
                            _radioVolumeDebounceTimer = Timer(const Duration(milliseconds: 180), () {
                              ref.read(apiServiceProvider).setVolume(value.round());
                            });
                          },
                          onChangeEnd: (value) async {
                            _radioVolumeDebounceTimer?.cancel();
                            final targetVol = value.round();
                            setState(() {
                              _localRadioVolume = targetVol.toDouble();
                            });
                            await ref.read(apiServiceProvider).setVolume(targetVol);
                            ref.read(statusPollingProvider.notifier).pollStatus();
                            Future.delayed(const Duration(milliseconds: 800), () {
                              if (mounted) {
                                setState(() {
                                  _isDraggingRadioVolume = false;
                                  _localRadioVolume = null;
                                });
                              }
                            });
                          },
                        ),
                      ),
                    ),
                    IconButton(
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(),
                      icon: const Icon(Icons.add_circle_outline_rounded, color: Color(0xFF00F2FE), size: 22),
                      onPressed: () async {
                        final current = (_localRadioVolume ?? status.volume.toDouble()).round();
                        if (current < 30) {
                          final newVol = current + 1;
                          setState(() {
                            _localRadioVolume = newVol.toDouble();
                            _isDraggingRadioVolume = true;
                          });
                          _radioVolumeDebounceTimer?.cancel();
                          await ref.read(apiServiceProvider).setVolume(newVol);
                          ref.read(statusPollingProvider.notifier).pollStatus();
                          Future.delayed(const Duration(milliseconds: 800), () {
                            if (mounted) {
                              setState(() {
                                _isDraggingRadioVolume = false;
                                _localRadioVolume = null;
                              });
                            }
                          });
                        }
                      },
                    ),
                  ],
                ),
              ],
            ),
          ),
          Container(
            padding: const EdgeInsets.all(16),
            decoration: BoxDecoration(
              color: const Color(0x7316132C),
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.white.withOpacity(0.08)),
            ),
            child: Column(
              children: [
                TextField(
                  controller: _searchController,
                  style: const TextStyle(color: Colors.white),
                  decoration: InputDecoration(
                    hintText: 'البحث عن إذاعة أو قارئ...',
                    hintStyle: GoogleFonts.cairo(color: Colors.white38, fontSize: 13),
                    prefixIcon: const Icon(Icons.search, color: Color(0xFF00F2FE)),
                    suffixIcon: _searchQuery.isNotEmpty
                        ? IconButton(
                            icon: const Icon(Icons.clear, color: Colors.white54),
                            onPressed: () {
                              _searchController.clear();
                              setState(() {
                                _searchQuery = '';
                              });
                              _filterRadios();
                            },
                          )
                        : null,
                    filled: true,
                    fillColor: Colors.black26,
                    border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                  ),
                  onChanged: (val) {
                    setState(() {
                      _searchQuery = val;
                    });
                    _filterRadios();
                  },
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: DropdownButtonFormField<String>(
                        isExpanded: true,
                        dropdownColor: const Color(0xFF1C132E),
                        style: const TextStyle(color: Colors.white, fontSize: 13),
                        value: _selectedCountry.isEmpty ? null : _selectedCountry,
                        hint: Text('فلترة حسب الدولة', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 12)),
                        decoration: InputDecoration(
                          filled: true,
                          fillColor: Colors.black26,
                          contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                          border: OutlineInputBorder(borderRadius: BorderRadius.circular(12), borderSide: BorderSide.none),
                        ),
                        items: const [
                          DropdownMenuItem(value: '', child: Text('جميع الدول', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'السعودية', child: Text('السعودية', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'مصر', child: Text('مصر', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'الكويت', child: Text('الكويت', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'الإمارات', child: Text('الإمارات', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'فلسطين', child: Text('فلسطين', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'الجزائر', child: Text('الجزائر', overflow: TextOverflow.ellipsis)),
                          DropdownMenuItem(value: 'المغرب', child: Text('المغرب')),
                          DropdownMenuItem(value: 'الأردن', child: Text('الأردن')),
                          DropdownMenuItem(value: 'اليمن', child: Text('اليمن')),
                          DropdownMenuItem(value: 'عمان', child: Text('عمان')),
                          DropdownMenuItem(value: 'البحرين', child: Text('البحرين')),
                          DropdownMenuItem(value: 'قطر', child: Text('قطر')),
                          DropdownMenuItem(value: 'ليبيا', child: Text('ليبيا')),
                        ],
                        onChanged: (val) {
                          setState(() {
                            _selectedCountry = val ?? '';
                          });
                          _filterRadios();
                        },
                      ),
                    ),
                    const SizedBox(width: 12),
                    ElevatedButton(
                      onPressed: () => _fetchRadios(force: true),
                      style: ElevatedButton.styleFrom(
                        backgroundColor: const Color(0xFF00F2FE).withOpacity(0.1),
                        foregroundColor: const Color(0xFF00F2FE),
                        side: BorderSide(color: const Color(0xFF00F2FE).withOpacity(0.3)),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                        padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 16),
                      ),
                      child: const Icon(Icons.refresh, size: 20),
                    ),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 12),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4.0),
            child: Text(
              _statusText,
              style: GoogleFonts.cairo(color: Colors.white60, fontSize: 11),
            ),
          ),
          const SizedBox(height: 12),
          Expanded(
            child: _isLoading
                ? const Center(child: CircularProgressIndicator(color: Color(0xFF00F2FE)))
                : _filteredRadios.isEmpty
                    ? Center(
                        child: Text(
                          'لا توجد إذاعات تطابق البحث.',
                          style: GoogleFonts.cairo(color: Colors.white38),
                        ),
                      )
                    : ListView.separated(
                        itemCount: _filteredRadios.length,
                        separatorBuilder: (context, index) => const SizedBox(height: 8),
                        itemBuilder: (context, index) {
                          final radio = _filteredRadios[index];
                          final name = radio['name'] as String? ?? 'إذاعة غير معروفة';
                          final url = radio['url'] as String? ?? '';

                          return Container(
                            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
                            decoration: BoxDecoration(
                              color: Colors.white.withOpacity(0.02),
                              borderRadius: BorderRadius.circular(16),
                              border: Border.all(color: Colors.white.withOpacity(0.04)),
                            ),
                            child: Row(
                              children: [
                                Expanded(
                                  child: Column(
                                    crossAxisAlignment: CrossAxisAlignment.start,
                                    children: [
                                      Text(
                                        name,
                                        style: GoogleFonts.cairo(
                                          color: Colors.white,
                                          fontSize: 13,
                                          fontWeight: FontWeight.bold,
                                        ),
                                      ),
                                      const SizedBox(height: 4),
                                      Text(
                                        url,
                                        style: GoogleFonts.orbitron(
                                          color: Colors.white38,
                                          fontSize: 10,
                                        ),
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                    ],
                                  ),
                                ),
                                const SizedBox(width: 8),
                                ElevatedButton(
                                  onPressed: () => _playRadio(url, name),
                                  style: ElevatedButton.styleFrom(
                                    backgroundColor: const Color(0xFF00F2FE).withOpacity(0.15),
                                    foregroundColor: const Color(0xFF00F2FE),
                                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                                  ),
                                  child: Text('تشغيل', style: GoogleFonts.cairo(fontSize: 11, fontWeight: FontWeight.bold)),
                                ),
                                const SizedBox(width: 8),
                                ElevatedButton(
                                  onPressed: widget.onSchedule == null
                                      ? null
                                      : () => widget.onSchedule!(url, name),
                                  style: ElevatedButton.styleFrom(
                                    backgroundColor: Colors.white.withOpacity(0.08),
                                    foregroundColor: Colors.white,
                                    padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
                                  ),
                                  child: Text('جدولة', style: GoogleFonts.cairo(fontSize: 11)),
                                ),
                              ],
                            ),
                          );
                        },
                      ),
          ),
        ],
      ),
    );
  }
}
