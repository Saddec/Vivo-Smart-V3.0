import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:google_fonts/google_fonts.dart';
import '../providers/app_state.dart';

class LoginScreen extends ConsumerStatefulWidget {
  const LoginScreen({super.key});

  @override
  ConsumerState<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends ConsumerState<LoginScreen> {
  final _ipController = TextEditingController();
  final _passwordController = TextEditingController();
  
  bool _isPasswordVisible = false;
  final _formKey = GlobalKey<FormState>();
  
  bool _isScanning = false;
  List<String> _foundIps = [];
  bool _isIpDropdownVisible = false;

  @override
  void initState() {
    super.initState();
    // Pre-fill last connected IP if it exists
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      final connectionState = ref.read(connectionStateProvider);
      if (connectionState.ip != null) {
        _ipController.text = connectionState.ip!;
      } else {
        _ipController.text = '192.168.1.100'; // Default placeholder
      }
    });
  }

  @override
  void dispose() {
    _ipController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  Future<void> _scanNetwork() async {
    setState(() {
      _isScanning = true;
      _foundIps = [];
      _isIpDropdownVisible = true;
    });

    final api = ref.read(apiServiceProvider);
    final ips = await api.scanNetworkForDevices();

    if (mounted) {
      setState(() {
        _isScanning = false;
        _foundIps = ips;
        if (ips.isNotEmpty) {
          _ipController.text = ips.first;
        }
      });
    }
  }

  Future<void> _handleConnectAndLogin() async {
    if (!_formKey.currentState!.validate()) return;
    
    final ip = _ipController.text.trim();
    final password = _passwordController.text;

    final notifier = ref.read(connectionStateProvider.notifier);
    
    // Step 1: Connect (check connection)
    final connected = await notifier.connectToDevice(ip);
    if (!connected) return;

    // Step 2: Login (check password)
    final loggedIn = await notifier.login(password);
    if (loggedIn) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('تم الاتصال وتسجيل الدخول بنجاح!'),
            backgroundColor: Colors.green,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final connectionState = ref.watch(connectionStateProvider);

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
          body: Center(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(24.0),
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 400),
                child: Form(
                  key: _formKey,
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          shape: BoxShape.circle,
                          boxShadow: [
                            BoxShadow(
                              color: Colors.white.withOpacity(0.1),
                              blurRadius: 30,
                              spreadRadius: 5,
                            ),
                          ],
                        ),
                        child: Image.asset(
                          'assets/logo.png',
                          height: 120,
                          fit: BoxFit.contain,
                        ),
                      ),
                      const SizedBox(height: 16),
                      Text(
                        'VIVO SMART',
                        textAlign: TextAlign.center,
                        style: GoogleFonts.cairo(
                          fontSize: 32,
                          fontWeight: FontWeight.w900,
                          letterSpacing: 2,
                          color: Colors.white,
                          shadows: [
                            BoxShadow(
                              color: Colors.black.withOpacity(0.3),
                              blurRadius: 15,
                            ),
                          ],
                        ),
                      ),
                      Text(
                        'نظام التحكم الذكي بالصوت ومواقيت الأذان',
                        textAlign: TextAlign.center,
                        style: GoogleFonts.cairo(
                          fontSize: 14,
                          fontWeight: FontWeight.bold,
                          color: Colors.white.withOpacity(0.9),
                        ),
                      ),
                      const SizedBox(height: 40),
                      
                      // Glassmorphic Form Card
                      Container(
                        padding: const EdgeInsets.all(24),
                        decoration: BoxDecoration(
                          color: const Color(0x7316132C), // Web's glass-bg rgba(22, 19, 44, 0.45)
                          borderRadius: BorderRadius.circular(24),
                          border: Border.all(
                            color: Colors.white.withOpacity(0.08), // Web's glass-border rgba(255, 255, 255, 0.08)
                            width: 1,
                          ),
                          boxShadow: [
                            BoxShadow(
                              color: Colors.black.withOpacity(0.4),
                              blurRadius: 30,
                              offset: const Offset(0, 10),
                            ),
                          ],
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            Text(
                              'الاتصال بالجهاز',
                              style: GoogleFonts.cairo(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                                color: Colors.white,
                              ),
                            ),
                            const SizedBox(height: 20),
                            
                            // IP Input with Scan Button
                            Row(
                              crossAxisAlignment: CrossAxisAlignment.center,
                              children: [
                                Expanded(
                                  child: TextFormField(
                                    controller: _ipController,
                                    style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
                                    keyboardType: TextInputType.url,
                                    textAlign: TextAlign.left,
                                    decoration: InputDecoration(
                                      labelText: 'عنوان IP لجهاز التحكم',
                                      labelStyle: GoogleFonts.cairo(color: Colors.white70, fontSize: 13, fontWeight: FontWeight.bold),
                                      prefixIcon: const Icon(Icons.router, color: Colors.white70),
                                      suffixIcon: _foundIps.isNotEmpty && _isIpDropdownVisible
                                          ? IconButton(
                                              icon: const Icon(Icons.keyboard_arrow_down, color: Colors.white70),
                                              onPressed: () {
                                                setState(() {
                                                  _isIpDropdownVisible = !_isIpDropdownVisible;
                                                });
                                              },
                                            )
                                          : null,
                                      filled: true,
                                      fillColor: Colors.white.withOpacity(0.05),
                                      border: OutlineInputBorder(
                                        borderRadius: BorderRadius.circular(16),
                                        borderSide: BorderSide(color: Colors.white.withOpacity(0.15)),
                                      ),
                                      enabledBorder: OutlineInputBorder(
                                        borderRadius: BorderRadius.circular(16),
                                        borderSide: BorderSide(color: Colors.white.withOpacity(0.1)),
                                      ),
                                      focusedBorder: OutlineInputBorder(
                                        borderRadius: BorderRadius.circular(16),
                                        borderSide: const BorderSide(color: Colors.white, width: 1.5),
                                      ),
                                    ),
                                    validator: (value) {
                                      if (value == null || value.trim().isEmpty) {
                                        return 'الرجاء إدخال عنوان IP الخاص بالجهاز';
                                      }
                                      return null;
                                    },
                                  ),
                                ),
                                const SizedBox(width: 8),
                                SizedBox(
                                  height: 50,
                                  child: ElevatedButton(
                                    onPressed: _isScanning ? null : _scanNetwork,
                                    style: ElevatedButton.styleFrom(
                                      padding: const EdgeInsets.symmetric(horizontal: 12),
                                      shape: RoundedRectangleBorder(
                                        borderRadius: BorderRadius.circular(16),
                                        side: BorderSide(color: Colors.white.withOpacity(0.15)),
                                      ),
                                      backgroundColor: Colors.white.withOpacity(0.08),
                                      foregroundColor: Colors.white,
                                    ),
                                    child: _isScanning
                                        ? const SizedBox(
                                            height: 20,
                                            width: 20,
                                            child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2),
                                          )
                                        : Row(
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                              const Icon(Icons.wifi_find, size: 18),
                                              const SizedBox(width: 4),
                                              Text(
                                                'بحث',
                                                style: GoogleFonts.cairo(fontWeight: FontWeight.bold, fontSize: 13),
                                              ),
                                            ],
                                          ),
                                  ),
                                ),
                              ],
                            ),
                            // Found IPs Dropdown
                            if (_isIpDropdownVisible && _foundIps.isNotEmpty) ...[
                              const SizedBox(height: 8),
                              Container(
                                decoration: BoxDecoration(
                                  color: Colors.white.withOpacity(0.08),
                                  borderRadius: BorderRadius.circular(12),
                                  border: Border.all(color: Colors.white.withOpacity(0.1)),
                                ),
                                constraints: const BoxConstraints(maxHeight: 150),
                                child: ListView.separated(
                                  itemCount: _foundIps.length,
                                  separatorBuilder: (_, __) => const Divider(color: Colors.white10, height: 1),
                                  itemBuilder: (context, index) {
                                    final ip = _foundIps[index];
                                    return ListTile(
                                      dense: true,
                                      leading: const Icon(Icons.check_circle, color: Colors.greenAccent, size: 18),
                                      title: Text(
                                        ip,
                                        style: GoogleFonts.cairo(color: Colors.white, fontWeight: FontWeight.bold),
                                        textDirection: TextDirection.ltr,
                                      ),
                                      onTap: () {
                                        _ipController.text = ip;
                                        setState(() {
                                          _isIpDropdownVisible = false;
                                        });
                                      },
                                    );
                                  },
                                ),
                              ),
                            ] else if (_isScanning) ...[
                              const SizedBox(height: 8),
                              Container(
                                padding: const EdgeInsets.symmetric(vertical: 12),
                                decoration: BoxDecoration(
                                  color: Colors.white.withOpacity(0.05),
                                  borderRadius: BorderRadius.circular(12),
                                ),
                                child: Row(
                                  mainAxisAlignment: MainAxisAlignment.center,
                                  children: [
                                    const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(color: Colors.white38, strokeWidth: 2)),
                                    const SizedBox(width: 12),
                                    Text(
                                      'جاري البحث عن الأجهزة في الشبكة...',
                                      style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
                                    ),
                                  ],
                                ),
                              ),
                            ] else if (_isIpDropdownVisible && !_isScanning && _foundIps.isEmpty) ...[
                              const SizedBox(height: 8),
                              Container(
                                padding: const EdgeInsets.symmetric(vertical: 12),
                                decoration: BoxDecoration(
                                  color: Colors.white.withOpacity(0.05),
                                  borderRadius: BorderRadius.circular(12),
                                ),
                                child: Row(
                                  mainAxisAlignment: MainAxisAlignment.center,
                                  children: [
                                    const Icon(Icons.search_off, color: Colors.white38, size: 16),
                                    const SizedBox(width: 12),
                                    Text(
                                      'لم يتم العثور على أجهزة',
                                      style: GoogleFonts.cairo(color: Colors.white60, fontSize: 12),
                                    ),
                                  ],
                                ),
                              ),
                            ],
                            
                            // Saved Devices List
                            Builder(
                              builder: (context) {
                                final savedDevices = ref.watch(savedDevicesProvider);
                                if (savedDevices.isEmpty) return const SizedBox.shrink();

                                return Column(
                                  crossAxisAlignment: CrossAxisAlignment.stretch,
                                  children: [
                                    const SizedBox(height: 16),
                                    Align(
                                      alignment: Alignment.centerRight,
                                      child: Text(
                                        'الأجهزة المحفوظة (اضغط للاتصال السريع):',
                                        style: GoogleFonts.cairo(
                                          color: const Color(0xFFADCCFF), // text-secondary
                                          fontSize: 13,
                                          fontWeight: FontWeight.bold,
                                        ),
                                      ),
                                    ),
                                    const SizedBox(height: 8),
                                    Container(
                                      decoration: BoxDecoration(
                                        color: Colors.white.withOpacity(0.02),
                                        borderRadius: BorderRadius.circular(16),
                                        border: Border.all(color: Colors.white.withOpacity(0.08)),
                                      ),
                                      child: ListView.separated(
                                        shrinkWrap: true,
                                        physics: const NeverScrollableScrollPhysics(),
                                        itemCount: savedDevices.length,
                                        separatorBuilder: (_, __) => const Divider(color: Colors.white10, height: 1),
                                        itemBuilder: (context, index) {
                                          final dev = savedDevices[index];
                                          final displayName = dev.name.isNotEmpty && dev.name != dev.ip
                                              ? '${dev.name} (${dev.ip})'
                                              : dev.ip;
                                          return ListTile(
                                            dense: true,
                                            leading: const Icon(Icons.router, color: Color(0xFF00F2FE), size: 20),
                                            title: Text(
                                              displayName,
                                              style: GoogleFonts.cairo(color: Colors.white, fontSize: 13, fontWeight: FontWeight.bold),
                                              textDirection: TextDirection.ltr,
                                            ),
                                            subtitle: dev.password.isNotEmpty
                                                ? Text('كلمة المرور محفوظة ✓', style: GoogleFonts.cairo(color: Colors.white38, fontSize: 11))
                                                : null,
                                            trailing: Row(
                                              mainAxisSize: MainAxisSize.min,
                                              children: [
                                                IconButton(
                                                  icon: const Icon(Icons.login, color: Color(0xFF00F2FE), size: 20),
                                                  tooltip: 'اتصال فوري',
                                                  onPressed: () {
                                                    _ipController.text = dev.ip;
                                                    _passwordController.text = dev.password;
                                                    _handleConnectAndLogin();
                                                  },
                                                ),
                                                IconButton(
                                                  icon: const Icon(Icons.delete_outline, color: Colors.redAccent, size: 18),
                                                  tooltip: 'حذف',
                                                  onPressed: () async {
                                                    await ref.read(savedDevicesProvider.notifier).removeDevice(dev.ip);
                                                  },
                                                ),
                                              ],
                                            ),
                                            onTap: () {
                                              _ipController.text = dev.ip;
                                              _passwordController.text = dev.password;
                                            },
                                          );
                                        },
                                      ),
                                    ),
                                  ],
                                );
                              },
                            ),

                            const SizedBox(height: 16),
                            
                            // Password Input
                            TextFormField(
                              controller: _passwordController,
                              obscureText: !_isPasswordVisible,
                              style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
                              decoration: InputDecoration(
                                labelText: 'كلمة المرور',
                                labelStyle: GoogleFonts.cairo(color: Colors.white70, fontSize: 13, fontWeight: FontWeight.bold),
                                prefixIcon: const Icon(Icons.lock_open, color: Colors.white70),
                                suffixIcon: IconButton(
                                  icon: Icon(
                                    _isPasswordVisible ? Icons.visibility : Icons.visibility_off,
                                    color: Colors.white70,
                                  ),
                                  onPressed: () {
                                    setState(() {
                                      _isPasswordVisible = !_isPasswordVisible;
                                    });
                                  },
                                ),
                                filled: true,
                                fillColor: Colors.white.withOpacity(0.05),
                                border: OutlineInputBorder(
                                  borderRadius: BorderRadius.circular(16),
                                  borderSide: BorderSide(color: Colors.white.withOpacity(0.15)),
                                ),
                                enabledBorder: OutlineInputBorder(
                                  borderRadius: BorderRadius.circular(16),
                                  borderSide: BorderSide(color: Colors.white.withOpacity(0.1)),
                                ),
                                focusedBorder: OutlineInputBorder(
                                  borderRadius: BorderRadius.circular(16),
                                  borderSide: const BorderSide(color: Colors.white, width: 1.5),
                                ),
                              ),
                              validator: (value) {
                                if (value == null || value.isEmpty) {
                                  return 'الرجاء إدخال كلمة المرور';
                                }
                                return null;
                              },
                            ),
                            const SizedBox(height: 24),
                            
                            // Error message display
                            if (connectionState.error != null) ...[
                              Container(
                                padding: const EdgeInsets.all(12),
                                decoration: BoxDecoration(
                                  color: Colors.redAccent.withOpacity(0.15),
                                  borderRadius: BorderRadius.circular(12),
                                  border: Border.all(color: Colors.redAccent.withOpacity(0.3)),
                                ),
                                child: Text(
                                  connectionState.error!,
                                  textAlign: TextAlign.center,
                                  style: GoogleFonts.cairo(
                                    color: Colors.redAccent,
                                    fontSize: 13,
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                              ),
                              const SizedBox(height: 16),
                            ],
                            
                            // Action Button
                            Container(
                              decoration: BoxDecoration(
                                gradient: const LinearGradient(
                                  colors: [
                                    Color(0xFF8E44AD), // Primary Purple
                                    Color(0xFF3498DB), // Primary Blue
                                  ],
                                  begin: Alignment.centerRight,
                                  end: Alignment.centerLeft,
                                ),
                                borderRadius: BorderRadius.circular(16),
                                boxShadow: [
                                  BoxShadow(
                                    color: const Color(0xFF8E44AD).withOpacity(0.4),
                                    blurRadius: 12,
                                    offset: const Offset(0, 4),
                                  ),
                                ],
                              ),
                              child: ElevatedButton(
                                onPressed: connectionState.isConnecting ? null : _handleConnectAndLogin,
                                style: ElevatedButton.styleFrom(
                                  padding: const EdgeInsets.symmetric(vertical: 16),
                                  shape: RoundedRectangleBorder(
                                    borderRadius: BorderRadius.circular(16),
                                  ),
                                  backgroundColor: Colors.transparent,
                                  shadowColor: Colors.transparent,
                                  foregroundColor: Colors.white,
                                ),
                                child: connectionState.isConnecting
                                    ? const SizedBox(
                                        height: 20,
                                        width: 20,
                                        child: CircularProgressIndicator(
                                          color: Colors.white,
                                          strokeWidth: 2,
                                        ),
                                      )
                                    : Text(
                                        'اتصال ودخول',
                                        style: GoogleFonts.cairo(
                                          fontSize: 16,
                                          fontWeight: FontWeight.bold,
                                          color: Colors.white,
                                        ),
                                      ),
                              ),
                            ),
                            const SizedBox(height: 16),
                            Center(
                              child: Text(
                                'Vivo Smart v1.0.4',
                                style: GoogleFonts.cairo(
                                  color: Colors.white38,
                                  fontSize: 12,
                                  fontWeight: FontWeight.w600,
                                  letterSpacing: 0.5,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
