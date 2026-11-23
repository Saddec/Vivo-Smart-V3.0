import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:google_fonts/google_fonts.dart';
import 'providers/app_state.dart';
import 'screens/login_screen.dart';
import 'screens/dashboard_screen.dart';
import 'services/notification_service.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  GoogleFonts.config.allowRuntimeFetching = false;
  await NotificationService.instance.initialize();
  runApp(
    const ProviderScope(
      child: MyApp(),
    ),
  );
}

class MyApp extends ConsumerWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final connectionState = ref.watch(connectionStateProvider);

    return MaterialApp(
      title: 'VivoSmart',
      debugShowCheckedModeBanner: false,
      // Apply sleek dark mode theme with Cairo font
      themeMode: ThemeMode.dark,
      theme: ThemeData(
        brightness: Brightness.dark,
        primaryColor: const Color(0xFF8E44AD),
        scaffoldBackgroundColor: const Color(0xFF2C1340),
        snackBarTheme: SnackBarThemeData(
          behavior: SnackBarBehavior.floating,
          backgroundColor: const Color(0xFF1E1735),
          contentTextStyle: GoogleFonts.cairo(color: Colors.white, fontSize: 13),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
          elevation: 8,
          insetPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
        ),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF8E44AD),
          secondary: Color(0xFF00F2FE), // Accent Cyan
          surface: Colors.transparent,
        ),
        textTheme: GoogleFonts.cairoTextTheme(ThemeData.dark().textTheme).copyWith(
          bodyLarge: GoogleFonts.cairo(color: Colors.white, fontSize: 16),
          bodyMedium: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 14), // Ice blue text
          titleLarge: GoogleFonts.cairo(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold),
        ),
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.transparent,
          elevation: 0,
          titleTextStyle: GoogleFonts.cairo(
            color: Colors.white,
            fontSize: 20,
            fontWeight: FontWeight.bold,
          ),
          iconTheme: const IconThemeData(color: Colors.white),
        ),
      ),
      darkTheme: ThemeData(
        brightness: Brightness.dark,
        primaryColor: const Color(0xFF8E44AD),
        scaffoldBackgroundColor: const Color(0xFF2C1340),
        snackBarTheme: SnackBarThemeData(
          behavior: SnackBarBehavior.floating,
          backgroundColor: const Color(0xFF1E1735),
          contentTextStyle: GoogleFonts.cairo(color: Colors.white, fontSize: 13),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
          elevation: 8,
          insetPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
        ),
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF8E44AD),
          secondary: Color(0xFF00F2FE), // Accent Cyan
          surface: Colors.transparent,
        ),
        textTheme: GoogleFonts.cairoTextTheme(ThemeData.dark().textTheme).copyWith(
          bodyLarge: GoogleFonts.cairo(color: Colors.white, fontSize: 16),
          bodyMedium: GoogleFonts.cairo(color: const Color(0xFFADCCFF), fontSize: 14), // Ice blue text
          titleLarge: GoogleFonts.cairo(color: Colors.white, fontSize: 20, fontWeight: FontWeight.bold),
        ),
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.transparent,
          elevation: 0,
          titleTextStyle: GoogleFonts.cairo(
            color: Colors.white,
            fontSize: 20,
            fontWeight: FontWeight.bold,
          ),
          iconTheme: const IconThemeData(color: Colors.white),
        ),
      ),
      home: connectionState.isLoggedIn ? const DashboardScreen() : const LoginScreen(),
    );
  }
}
