import 'dart:convert';
import 'dart:io';
import 'package:dio/dio.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:shared_preferences/shared_preferences.dart';

class ApiService {
  final Dio _dio = Dio();
  final _secureStorage = const FlutterSecureStorage();
  
  String? _ipAddress;
  String? _sessionToken;
  VoidCallback? onUnauthorized;

  ApiService() {
    _dio.options.connectTimeout = const Duration(seconds: 5);
    _dio.options.receiveTimeout = const Duration(seconds: 5);
    _dio.options.headers['ngrok-skip-browser-warning'] = '69420';
    
    // Add interceptors to automatically inject session token and handle 401 responses
    _dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (options, handler) {
          if (_sessionToken != null && _sessionToken!.isNotEmpty) {
            options.headers['X-Session-Token'] = _sessionToken;
            
            // Also append token to query parameters for extra compatibility
            if (options.method == 'GET') {
              final uri = options.uri;
              final queryParams = Map<String, dynamic>.from(uri.queryParameters);
              if (!queryParams.containsKey('token')) {
                queryParams['token'] = _sessionToken;
                options.queryParameters = queryParams;
              }
            }
          }
          return handler.next(options);
        },
        onError: (DioException e, handler) {
          if (e.response?.statusCode == 401) {
            // Trigger logout / unauthorized callback on the UI layer
            if (onUnauthorized != null) {
              onUnauthorized!();
            }
          }
          return handler.next(e);
        },
      ),
    );
  }

  String? get ipAddress => _ipAddress;
  String? get sessionToken => _sessionToken;

  // Format input IP/URL to ensure it has valid HTTP/HTTPS scheme and no trailing slashes
  String _formatBaseUrl(String input) {
    var url = input.trim();
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      // Automatically wrap raw IPv6 addresses in brackets [...]
      if (url.contains(':') && !url.startsWith('[') && ':'.allMatches(url).length >= 2) {
        url = 'http://[$url]';
      } else {
        url = 'http://$url';
      }
    }
    while (url.endsWith('/')) {
      url = url.substring(0, url.length - 1);
    }
    return url;
  }

  // Initialize service by loading saved IP and token from local storage
  Future<void> init() async {
    final prefs = await SharedPreferences.getInstance();
    _ipAddress = prefs.getString('vivo_ip');
    _sessionToken = await _secureStorage.read(key: 'vivo_token');
    
    if (_ipAddress != null) {
      _dio.options.baseUrl = _formatBaseUrl(_ipAddress!);
    }
  }

  // Set local IP address
  Future<void> setIpAddress(String ip) async {
    _ipAddress = ip;
    _dio.options.baseUrl = _formatBaseUrl(ip);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('vivo_ip', ip);
  }

  // Set session token
  Future<void> setSessionToken(String? token) async {
    _sessionToken = token;
    if (token != null) {
      await _secureStorage.write(key: 'vivo_token', value: token);
    } else {
      await _secureStorage.delete(key: 'vivo_token');
    }
  }

  // Clear IP and Token on logout
  Future<void> logout() async {
    await setSessionToken(null);
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove('vivo_ip');
    _ipAddress = null;
  }

  // Check connection to Vivo Smart device
  Future<bool> checkConnection(String ip) async {
    try {
      final baseUrl = _formatBaseUrl(ip);
      final response = await Dio(
        BaseOptions(
          connectTimeout: const Duration(seconds: 8),
          receiveTimeout: const Duration(seconds: 8),
          headers: {
            'ngrok-skip-browser-warning': '69420',
          },
        ),
      ).get('$baseUrl/api/status');
      
      return response.statusCode == 200;
    } catch (e) {
      debugPrint('Connection check failed for IP $ip: $e');
      return false;
    }
  }

  // Authenticate user with password
  Future<Map<String, dynamic>> login(String ip, String password) async {
    try {
      final baseUrl = _formatBaseUrl(ip);
      final response = await Dio(
        BaseOptions(
          connectTimeout: const Duration(seconds: 8),
          receiveTimeout: const Duration(seconds: 8),
          headers: {
            'ngrok-skip-browser-warning': '69420',
          },
        ),
      ).post(
        '$baseUrl/api/password/check',
        data: 'password=${Uri.encodeComponent(password)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
            'ngrok-skip-browser-warning': '69420',
          },
        ),
      );

      if (response.statusCode == 200) {
        final data = response.data is String ? jsonDecode(response.data) : response.data;
        if (data['ok'] == true) {
          final token = data['token'] as String?;
          await setIpAddress(ip);
          await setSessionToken(token);
          return {'ok': true, 'token': token};
        } else {
          return {'ok': false, 'error': 'كلمة المرور غير صحيحة'};
        }
      }
      return {'ok': false, 'error': 'فشل الاتصال بالخادم'};
    } catch (e) {
      debugPrint('Login request failed: $e');
      return {'ok': false, 'error': 'تعذر الوصول للجهاز، تحقق من العنوان والشبكة'};
    }
  }

  // Fetch device status
  Future<Map<String, dynamic>> getStatus({bool isUserActive = true}) async {
    try {
      final response = await _dio.get('/api/status', queryParameters: {
        'active': isUserActive ? '1' : '0',
      });
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting status: $e');
      return {'ok': false, 'error': e.toString()};
    }
  }

  // Fetch current clock details (Gregorian & Hijri)
  Future<Map<String, dynamic>> getClock() async {
    try {
      final response = await _dio.get('/api/clock');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting clock: $e');
      return {};
    }
  }

  // Fetch prayer times for today
  Future<Map<String, dynamic>> getPrayerTimes() async {
    try {
      final response = await _dio.get('/api/prayer/times');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting prayer times: $e');
      return {};
    }
  }

  // Send volume update
  Future<bool> setVolume(int volume) async {
    try {
      final response = await _dio.post(
        '/api/audio/volume',
        data: 'volume=$volume',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error setting volume: $e');
      return false;
    }
  }

  // Stop audio playback
  Future<bool> stopAudio() async {
    try {
      final response = await _dio.post('/api/audio/stop');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error stopping audio: $e');
      return false;
    }
  }

  // Pause audio playback
  Future<bool> pauseAudio() async {
    try {
      final response = await _dio.post('/api/audio/pause');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error pausing audio: $e');
      return false;
    }
  }

  // Resume audio playback
  Future<bool> resumeAudio() async {
    try {
      final response = await _dio.post('/api/audio/resume');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error resuming audio: $e');
      return false;
    }
  }

  // Reboot device
  Future<bool> rebootSystem() async {
    try {
      final response = await _dio.post('/api/system/reboot');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error rebooting: $e');
      return false;
    }
  }

  // Factory Reset
  Future<bool> factoryReset() async {
    try {
      final response = await _dio.post('/api/system/reset');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error factory resetting: $e');
      return false;
    }
  }

  // Shutdown system
  Future<bool> shutdownSystem() async {
    try {
      final response = await _dio.post('/api/system/shutdown');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error shutting down: $e');
      return false;
    }
  }

  // --- Files APIs ---
  Future<Map<String, dynamic>> getFilesList() async {
    try {
      final response = await _dio.get('/api/files/list');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting files list: $e');
      return {};
    }
  }

  Future<bool> deleteFile(String path) async {
    try {
      final response = await _dio.post(
        '/api/files/delete',
        data: 'name=${Uri.encodeComponent(path)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting file: $e');
      return false;
    }
  }

  Future<bool> createDirectory(String path) async {
    try {
      final response = await _dio.post(
        '/api/files/mkdir',
        data: 'name=${Uri.encodeComponent(path)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error creating directory: $e');
      return false;
    }
  }

  Future<bool> renameFile(String oldPath, String newPath) async {
    try {
      final response = await _dio.post(
        '/api/files/rename',
        data: 'old=${Uri.encodeComponent(oldPath)}&new=${Uri.encodeComponent(newPath)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error renaming file: $e');
      return false;
    }
  }

  Future<bool> uploadFile({
    List<int>? bytes,
    String? filePath,
    required String filename,
    required String folder,
    void Function(int count, int total, double speedBps, String statusText)? onSendProgress,
    bool Function()? isCancelled,
  }) async {
    HttpClient? client;
    RandomAccessFile? raf;
    try {
      int total = 0;
      if (filePath != null && filePath.isNotEmpty) {
        final f = File(filePath);
        if (!await f.exists()) {
          debugPrint('File does not exist: $filePath');
          return false;
        }
        total = await f.length();
        raf = await f.open(mode: FileMode.read);
      } else if (bytes != null) {
        total = bytes.length;
      } else {
        return false;
      }

      if (total <= 0) return false;

      // 64KB chunk size matching script.js & ESP32 SPI SD card buffer
      const chunkSize = 65536;
      int offset = 0;
      final startTime = DateTime.now();

      final baseUrlStr = _dio.options.baseUrl.isNotEmpty
          ? _dio.options.baseUrl
          : (_ipAddress != null ? _formatBaseUrl(_ipAddress!) : '');
      if (baseUrlStr.isEmpty) return false;

      client = HttpClient();
      client.connectionTimeout = const Duration(seconds: 30);
      client.idleTimeout = const Duration(seconds: 30);

      while (offset < total) {
        if (isCancelled?.call() == true) {
          debugPrint('Upload cancelled by user');
          return false;
        }

        final end = (offset + chunkSize < total) ? offset + chunkSize : total;
        final currentChunkLen = end - offset;

        Uint8List chunk;
        if (raf != null) {
          await raf.setPosition(offset);
          chunk = await raf.read(currentChunkLen);
        } else {
          final sub = bytes!.sublist(offset, end);
          chunk = sub is Uint8List ? sub : Uint8List.fromList(sub);
        }

        final isFinal = (end == total);

        bool chunkUploaded = false;
        int retries = 0;
        const maxRetries = 3;

        while (!chunkUploaded && retries <= maxRetries) {
          if (isCancelled?.call() == true) return false;

          try {
            final queryParams = <String, String>{
              'name': filename,
              'folder': folder,
              'offset': offset.toString(),
              'total': total.toString(),
              'final': isFinal ? '1' : '0',
            };
            if (_sessionToken != null && _sessionToken!.isNotEmpty) {
              queryParams['token'] = _sessionToken!;
            }

            final baseUri = Uri.parse(baseUrlStr);
            final pathPrefix = baseUri.path.endsWith('/')
                ? baseUri.path.substring(0, baseUri.path.length - 1)
                : baseUri.path;
            final targetUri = baseUri.replace(
              path: '$pathPrefix/api/files/upload_chunk',
              queryParameters: queryParams,
            );

            final request = await client.postUrl(targetUri).timeout(const Duration(seconds: 35));
            request.headers.set('Content-Type', 'application/octet-stream');
            if (_sessionToken != null && _sessionToken!.isNotEmpty) {
              request.headers.set('X-Session-Token', _sessionToken!);
            }
            request.contentLength = chunk.length;
            request.add(chunk);

            final response = await request.close().timeout(const Duration(seconds: 35));
            final responseBody = await response.transform(utf8.decoder).join();

            if (response.statusCode == 200) {
              final data = jsonDecode(responseBody);
              if (data != null && data['ok'] == true) {
                chunkUploaded = true;
              } else {
                throw Exception(data?['error'] ?? 'write_failed');
              }
            } else {
              throw Exception('HTTP ${response.statusCode}: $responseBody');
            }
          } catch (e) {
            retries++;
            if (retries > maxRetries) {
              debugPrint('Chunk upload failed after $maxRetries retries at offset $offset: $e');
              return false;
            }
            final retryText = 'إعادة المحاولة $retries/$maxRetries...';
            if (onSendProgress != null) {
              final elapsedSec = DateTime.now().difference(startTime).inMilliseconds / 1000.0;
              final speed = elapsedSec > 0.3 ? (offset / elapsedSec) : 0.0;
              onSendProgress(offset, total, speed, retryText);
            }
            await Future.delayed(const Duration(milliseconds: 1000));
          }
        }

        offset = end;
        if (onSendProgress != null) {
          final elapsedSec = DateTime.now().difference(startTime).inMilliseconds / 1000.0;
          final speed = elapsedSec > 0.3 ? (offset / elapsedSec) : 0.0;
          onSendProgress(offset, total, speed, 'جاري الرفع...');
        }

        // Brief delay between chunks to allow ESP32 FreeRTOS tasks & SD flush
        await Future.delayed(const Duration(milliseconds: 15));
      }

      return true;
    } catch (e) {
      debugPrint('Error uploading file chunks: $e');
      return false;
    } finally {
      try {
        await raf?.close();
      } catch (_) {}
      try {
        client?.close(force: true);
      } catch (_) {}
    }
  }

  Future<bool> playDirectFile(String filePath) async {
    try {
      final response = await _dio.post(
        '/api/audio/play',
        data: 'file=${Uri.encodeComponent(filePath)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error playing file: $e');
      return false;
    }
  }

  // --- Adhan & Iqama settings ---
  Future<Map<String, dynamic>> getAdhanConfig() async {
    try {
      final response = await _dio.get('/api/adhan/files');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting adhan config: $e');
      return {};
    }
  }

  Future<bool> saveAdhanConfig(Map<String, String> params) async {
    try {
      final body = params.entries.map((e) => '${e.key}=${Uri.encodeComponent(e.value)}').join('&');
      final response = await _dio.post(
        '/api/adhan/files',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving adhan config: $e');
      return false;
    }
  }

  // --- Scheduler/Alerts APIs ---
  Future<List<dynamic>> getAlertsList() async {
    try {
      final response = await _dio.get('/api/scheduler/list');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is List ? data : [];
    } catch (e) {
      debugPrint('Error getting alerts list: $e');
      return [];
    }
  }

  Future<bool> deleteAlert(int index) async {
    try {
      final response = await _dio.post(
        '/api/scheduler/delete',
        data: 'index=$index',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting alert: $e');
      return false;
    }
  }

  Future<bool> toggleAlert(int index, bool enabled) async {
    try {
      final response = await _dio.post(
        '/api/scheduler/toggle',
        data: 'index=$index&enabled=${enabled ? 1 : 0}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error toggling alert: $e');
      return false;
    }
  }

  Future<bool> addOrEditAlert(Map<String, dynamic> alert, int index) async {
    try {
      final params = Map<String, dynamic>.from(alert);
      params['index'] = index;
      final body = params.entries.map((e) => '${e.key}=${Uri.encodeComponent(e.value.toString())}').join('&');
      final response = await _dio.post(
        '/api/scheduler/add',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving alert: $e');
      return false;
    }
  }

  // --- GPIO APIs ---
  Future<Map<String, dynamic>> getGpioMappings() async {
    try {
      final response = await _dio.get('/api/gpio/mappings');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting GPIO mappings: $e');
      return {};
    }
  }

  Future<bool> addInputMapping(Map<String, dynamic> mapping) async {
    try {
      final body = mapping.entries.map((e) => '${e.key}=${Uri.encodeComponent(e.value.toString())}').join('&');
      final response = await _dio.post(
        '/api/gpio/input',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error adding input mapping: $e');
      return false;
    }
  }

  Future<bool> deleteInputMapping(int pin) async {
    try {
      final response = await _dio.post(
        '/api/gpio/input/delete',
        data: 'pin=$pin',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting input mapping: $e');
      return false;
    }
  }

  Future<bool> addOutputMapping(int pin, String alertFile, int durationSec) async {
    try {
      final response = await _dio.post(
        '/api/gpio/output',
        data: 'pin=$pin&alert=${Uri.encodeComponent(alertFile)}&duration=$durationSec',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error adding output mapping: $e');
      return false;
    }
  }

  Future<List<dynamic>> getGpioSchedules() async {
    try {
      final response = await _dio.get('/api/gpio/schedule/list');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is List ? data : [];
    } catch (e) {
      debugPrint('Error getting GPIO schedules: $e');
      return [];
    }
  }

  Future<bool> addGpioSchedule(Map<String, dynamic> entry, int index) async {
    try {
      final params = Map<String, dynamic>.from(entry);
      params['index'] = index;
      final body = params.entries.map((e) => '${e.key}=${Uri.encodeComponent(e.value.toString())}').join('&');
      final response = await _dio.post(
        '/api/gpio/schedule/add',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving GPIO schedule: $e');
      return false;
    }
  }

  Future<bool> deleteGpioSchedule(int index) async {
    try {
      final response = await _dio.post(
        '/api/gpio/schedule/delete',
        data: 'index=$index',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting GPIO schedule: $e');
      return false;
    }
  }

  // --- Phase 3 APIs ---

  // Wi-Fi APIs
  Future<List<dynamic>> scanWifi() async {
    try {
      // Trigger scan or poll results up to 7 attempts (1s interval)
      for (int attempt = 0; attempt < 7; attempt++) {
        final response = await _dio.get('/api/wifi/scan');
        final data = response.data is String ? jsonDecode(response.data) : response.data;

        if (data is Map && data.containsKey('networks') && data['networks'] is List) {
          final list = List<dynamic>.from(data['networks']);
          list.sort((a, b) {
            final rssiA = (a['rssi'] as num?)?.toInt() ?? -100;
            final rssiB = (b['rssi'] as num?)?.toInt() ?? -100;
            return rssiB.compareTo(rssiA);
          });
          return list;
        } else if (data is List) {
          return data;
        }

        // If ESP responded with 202 {"status":"scanning"}, wait and retry
        await Future.delayed(const Duration(milliseconds: 1000));
      }
      return [];
    } catch (e) {
      debugPrint('Error scanning Wi-Fi: $e');
      return [];
    }
  }

  Future<bool> saveWifiConfig(String ssid, String password, {bool dhcp = true}) async {
    try {
      final response = await _dio.post(
        '/api/wifi/save',
        data: 'ssid=${Uri.encodeComponent(ssid)}&pass=${Uri.encodeComponent(password)}&dhcp=${dhcp ? 1 : 0}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Wi-Fi config: $e');
      return false;
    }
  }

  Future<bool> connectWifi() async {
    try {
      final response = await _dio.post('/api/wifi/connect');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error connecting to Wi-Fi: $e');
      return false;
    }
  }

  Future<Map<String, dynamic>> getWifiStatus() async {
    try {
      final response = await _dio.get('/api/wifi/status');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting Wi-Fi status: $e');
      return {};
    }
  }

  // Password Modification API
  Future<Map<String, dynamic>> changePassword(String oldPassword, String newPassword) async {
    try {
      final token = _sessionToken ?? '';
      final response = await _dio.post(
        '/api/password/change',
        data: 'token=${Uri.encodeComponent(token)}&old=${Uri.encodeComponent(oldPassword)}&password=${Uri.encodeComponent(newPassword)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data['ok'] == true) {
        final newToken = data['token'] as String?;
        if (newToken != null) {
          await setSessionToken(newToken);
        }
        return {'ok': true};
      }
      return {'ok': false, 'error': data['error'] ?? 'فشل تغيير كلمة المرور، يرجى التأكد من كلمة المرور القديمة'};
    } catch (e) {
      debugPrint('Error changing password: $e');
      return {'ok': false, 'error': 'فشل الاتصال بالخادم'};
    }
  }

  // System Logs APIs
  Future<List<dynamic>> getSystemLogs({int limit = 200, String level = ''}) async {
    try {
      final queryParams = <String, dynamic>{'limit': limit};
      if (level.isNotEmpty) {
        queryParams['level'] = level;
      }
      final response = await _dio.get('/api/logs', queryParameters: queryParams);
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is List ? data : [];
    } catch (e) {
      debugPrint('Error getting system logs: $e');
      return [];
    }
  }

  Future<bool> clearSystemLogs(String password) async {
    try {
      final response = await _dio.delete(
        '/api/logs',
        data: 'password=${Uri.encodeComponent(password)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error clearing system logs: $e');
      return false;
    }
  }

  // --- Quick Player APIs ---
  Future<bool> seekAudio(int seconds) async {
    try {
      final response = await _dio.post(
        '/api/audio/seek',
        data: 'seconds=$seconds',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error seeking audio: $e');
      return false;
    }
  }

  Future<Map<String, dynamic>> getTrackInfo() async {
    try {
      final response = await _dio.get('/api/audio/track_info');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting track info: $e');
      return {};
    }
  }

  Future<bool> playPlaylist(String files, int volume, bool respectAdhan) async {
    try {
      final response = await _dio.post(
        '/api/audio/playlist',
        data: 'files=${Uri.encodeComponent(files)}&volume=$volume&respectAdhan=${respectAdhan ? 1 : 0}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error playing playlist: $e');
      return false;
    }
  }

  // --- Eid Mode APIs ---
  Future<Map<String, dynamic>> getEidStatus() async {
    try {
      final response = await _dio.get('/api/eid/status');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting Eid status: $e');
      return {};
    }
  }

  Future<bool> toggleEidMode(bool manualEnabled, {bool? autoEnabled}) async {
    try {
      final params = <String>[
        'enabled=${manualEnabled ? 1 : 0}',
        if (autoEnabled != null) 'autoEnabled=${autoEnabled ? 1 : 0}',
      ];
      final response = await _dio.post(
        '/api/eid/mode',
        data: params.join('&'),
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error toggling Eid mode: $e');
      return false;
    }
  }

  Future<bool> saveEidFile(String file, int volume) async {
    try {
      final response = await _dio.post(
        '/api/eid/file',
        data: 'file=${Uri.encodeComponent(file)}&volume=$volume',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Eid file: $e');
      return false;
    }
  }

  Future<Map<String, dynamic>> getEidTakbeerConfig() async {
    try {
      final response = await _dio.get('/api/eid/takbeer_config');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting Eid takbeer config: $e');
      return {};
    }
  }

  Future<bool> saveEidTakbeerConfig(List<dynamic> prayers) async {
    try {
      final jsonStr = jsonEncode(prayers);
      final response = await _dio.post(
        '/api/eid/takbeer_config/save',
        data: 'json=${Uri.encodeComponent(jsonStr)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Eid takbeer config: $e');
      return false;
    }
  }

  // --- Maghrib Alerts APIs ---
  Future<Map<String, dynamic>> getMaghribConfig() async {
    try {
      final response = await _dio.get('/api/maghrib/alerts');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data is Map<String, dynamic>) {
        return data;
      } else if (data is Map) {
        return Map<String, dynamic>.from(data);
      } else if (data is List) {
        return {'alerts': data, 'offset': 1, 'skipAdhan': false, 'skipIqama': false};
      }
      return {'alerts': [], 'offset': 1, 'skipAdhan': false, 'skipIqama': false};
    } catch (e) {
      debugPrint('Error getting Maghrib config: $e');
      return {'alerts': [], 'offset': 1, 'skipAdhan': false, 'skipIqama': false};
    }
  }

  Future<List<dynamic>> getMaghribAlerts() async {
    try {
      final response = await _dio.get('/api/maghrib/alerts');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is List ? data : (data['alerts'] is List ? data['alerts'] : []);
    } catch (e) {
      debugPrint('Error getting Maghrib alerts: $e');
      return [];
    }
  }

  Future<bool> saveMaghribAlerts(dynamic alertsOrConfig) async {
    try {
      final jsonStr = jsonEncode(alertsOrConfig);
      final response = await _dio.post(
        '/api/maghrib/alert/save',
        data: 'json=${Uri.encodeComponent(jsonStr)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Maghrib alerts: $e');
      return false;
    }
  }

  /// Get all local IPv4 subnets from active network interfaces, excluding loopback and cellular.
  Future<List<String>> _getLocalSubnets() async {
    final subnets = <String>[];
    try {
      final interfaces = await NetworkInterface.list();
      for (final iface in interfaces) {
        final name = iface.name.toLowerCase();
        // Exclude mobile data/cellular interfaces to avoid scanning carrier networks
        if (name.contains('rmnet') ||
            name.contains('pdp') ||
            name.contains('ccmni') ||
            name.contains('wwan') ||
            name.contains('ppp')) {
          continue;
        }
        for (final addr in iface.addresses) {
          if (addr.type == InternetAddressType.IPv4 && !addr.isLoopback) {
            final parts = addr.address.split('.');
            if (parts.length == 4) {
              final s = '${parts[0]}.${parts[1]}.${parts[2]}';
              if (!subnets.contains(s)) {
                subnets.add(s);
              }
            }
          }
        }
      }
    } catch (_) {}
    return subnets;
  }

  // Scan local network for Vivo Smart devices
  Future<List<String>> scanNetworkForDevices({void Function(String ip, bool found)? onProgress}) async {
    final results = <String>[];
    final seen = <String>{};

    // Build subnet list: local first, then saved IP, then common + hotspot
    final subnets = <String>[];

    // 1) Local IP-derived subnets (handles active Wi-Fi and hotspot interfaces)
    final localSubnets = await _getLocalSubnets();
    subnets.addAll(localSubnets);

    // 2) Saved device IP subnet
    if (_ipAddress != null) {
      final parts = _ipAddress!.split('.');
      if (parts.length == 4) {
        final s = '${parts[0]}.${parts[1]}.${parts[2]}';
        if (!subnets.contains(s)) subnets.add(s);
      }
    }

    // 3) Common router subnets
    const common = ['192.168.1', '192.168.0', '10.0.0'];
    for (final s in common) {
      if (!subnets.contains(s)) subnets.add(s);
    }

    // 4) Hotspot subnets (Android, iPhone)
    const hotspots = ['192.168.43', '192.168.42', '172.20.10', '192.168.44'];
    for (final s in hotspots) {
      if (!subnets.contains(s)) subnets.add(s);
    }

    final scanDio = Dio(BaseOptions(
      connectTimeout: const Duration(milliseconds: 1000),
      receiveTimeout: const Duration(milliseconds: 1000),
    ));

    for (final subnet in subnets) {
      var foundInThisSubnet = false;
      const batchSize = 30;
      for (var start = 1; start <= 254; start += batchSize) {
        final end = (start + batchSize - 1).clamp(1, 254);
        final futures = <Future<String?>>[];
        for (var i = start; i <= end; i++) {
          final ip = '$subnet.$i';
          futures.add(_checkDeviceAlive(scanDio, ip));
        }
        final batchResults = await Future.wait(futures, eagerError: false);
        for (final result in batchResults) {
          if (result != null && seen.add(result)) {
            results.add(result);
            onProgress?.call(result, true);
            foundInThisSubnet = true;
          }
        }
      }
      // If we found at least one device in this subnet, we assume this is the active network
      // and skip scanning the other fallback subnets to save time.
      if (foundInThisSubnet) {
        break;
      }
    }
    scanDio.close(force: true);
    return results;
  }

  Future<String?> _checkDeviceAlive(Dio dio, String ip) async {
    try {
      final response = await dio.get('http://$ip/api/status');
      if (response.statusCode == 200) {
        return ip;
      }
    } catch (_) {}
    return null;
  }

  Future<int> getMaghribOffset() async {
    try {
      final response = await _dio.get('/api/maghrib/offset');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data is Map && data['offset'] != null) {
        return int.tryParse(data['offset'].toString()) ?? 1;
      }
      return 1;
    } catch (e) {
      debugPrint('Error getting Maghrib offset: $e');
      return 1;
    }
  }

  Future<bool> saveMaghribOffset(int offset) async {
    try {
      final response = await _dio.post(
        '/api/maghrib/offset',
        data: 'offset=$offset',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Maghrib offset: $e');
      return false;
    }
  }

  // Save Network (DHCP / Static IP) configuration
  Future<bool> saveNetworkConfig({
    required String ssid,
    required String password,
    required bool dhcp,
    required String ip,
    required String gateway,
    required String mask,
    required String dns,
  }) async {
    try {
      final response = await _dio.post(
        '/api/wifi/save',
        data: 'ssid=${Uri.encodeComponent(ssid)}&pass=${Uri.encodeComponent(password)}&dhcp=${dhcp ? 1 : 0}&ip=${Uri.encodeComponent(ip)}&gw=${Uri.encodeComponent(gateway)}&mask=${Uri.encodeComponent(mask)}&dns=${Uri.encodeComponent(dns)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving network config: $e');
      return false;
    }
  }

  // --- DDNS & Global IPv6 APIs ---
  Future<Map<String, dynamic>> getDdnsConfig() async {
    try {
      final response = await _dio.get('/api/ddns/config');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is Map<String, dynamic> ? data : {};
    } catch (e) {
      debugPrint('Error getting DDNS config: $e');
      return {};
    }
  }

  Future<bool> saveDdnsConfig({
    required bool enabled,
    required String provider,
    required String domain,
    required String user,
    required String pass,
    required bool useIPv6,
  }) async {
    try {
      final response = await _dio.post(
        '/api/ddns/save',
        data: 'enabled=${enabled ? 1 : 0}&provider=${Uri.encodeComponent(provider)}&domain=${Uri.encodeComponent(domain)}&user=${Uri.encodeComponent(user)}&pass=${Uri.encodeComponent(pass)}&useIPv6=${useIPv6 ? 1 : 0}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving DDNS config: $e');
      return false;
    }
  }

  // --- Telegram Remote Control Bot APIs ---
  Future<Map<String, dynamic>> getTelegramConfig() async {
    try {
      final response = await _dio.get('/api/telegram/config');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is Map<String, dynamic> ? data : {};
    } catch (e) {
      debugPrint('Error getting Telegram config: $e');
      return {};
    }
  }

  Future<bool> saveTelegramConfig({
    required bool enabled,
    required String token,
    required String chatId,
  }) async {
    try {
      final response = await _dio.post(
        '/api/telegram/save',
        data: 'enabled=${enabled ? 1 : 0}&token=${Uri.encodeComponent(token)}&chatId=${Uri.encodeComponent(chatId)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving Telegram config: $e');
      return false;
    }
  }

  // --- OTA Firmware / LittleFS Update API ---
  Future<bool> uploadOtaUpdate({
    required String filePath,
    required String filename,
    void Function(int sent, int total)? onSendProgress,
  }) async {
    try {
      final tokenParam = (_sessionToken != null && _sessionToken!.isNotEmpty)
          ? '?token=${Uri.encodeComponent(_sessionToken!)}'
          : '';
      final formData = FormData.fromMap({
        'update': await MultipartFile.fromFile(filePath, filename: filename),
        if (_sessionToken != null && _sessionToken!.isNotEmpty) 'token': _sessionToken,
      });
      final response = await _dio.post(
        '/api/ota$tokenParam',
        data: formData,
        onSendProgress: onSendProgress,
        options: Options(
          sendTimeout: const Duration(minutes: 5),
          receiveTimeout: const Duration(minutes: 2),
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return response.statusCode == 200 && (data is! Map || data['ok'] != false);
    } catch (e) {
      debugPrint('Error uploading OTA update: $e');
      return false;
    }
  }

  // Save manual clock settings
  Future<bool> saveManualClock(String time, String date, bool useNtp) async {
    try {
      final response = await _dio.post(
        '/api/clock/save',
        data: 'time=${Uri.encodeComponent(time)}&date=${Uri.encodeComponent(date)}&ntp=${useNtp ? 1 : 0}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving manual clock: $e');
      return false;
    }
  }

  // Save manual prayer times
  Future<bool> saveManualPrayers(Map<String, String> times) async {
    try {
      final dataStr = times.entries.map((e) => '${e.key}=${Uri.encodeComponent(e.value)}').join('&');
      final response = await _dio.post(
        '/api/prayer/manual/save',
        data: dataStr,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving manual prayers: $e');
      return false;
    }
  }

  // Save prayer source and calendar config
  Future<bool> savePrayerConfig(String source, {int hijriOffset = 0, String calcMethod = ''}) async {
    try {
      final isCalendar = source == 'calendar' || source == 'روزمانة';
      final isManual = source == 'manual' || source == 'يدوي';
      
      bool success = true;
      if (isManual) {
        final r1 = await _dio.post(
          '/api/prayer/manual/save',
          queryParameters: {'enabled': 1},
          data: 'enabled=1',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/csv/toggle',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/calendar/only',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        success = (r1.data['ok'] == true || r1.data is String && r1.data.contains('"ok":true'));
      } else if (isCalendar) {
        await _dio.post(
          '/api/prayer/manual/save',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        final r2 = await _dio.post(
          '/api/csv/toggle',
          queryParameters: {'enabled': 1},
          data: 'enabled=1',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/calendar/only',
          queryParameters: {'enabled': 1},
          data: 'enabled=1',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/prayer/offsets',
          data: 'fajr=0&dhuhr=0&asr=0&maghrib=0&isha=0&hijriOffset=$hijriOffset',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        success = (r2.data['ok'] == true || r2.data is String && r2.data.contains('"ok":true'));
      } else {
        await _dio.post(
          '/api/prayer/manual/save',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/calendar/only',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        await _dio.post(
          '/api/csv/toggle',
          queryParameters: {'enabled': 0},
          data: 'enabled=0',
          options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
        );
        
        if (calcMethod.isNotEmpty) {
          int methodIdx = 1;
          if (calcMethod == 'Egypt') methodIdx = 0;
          if (calcMethod == 'UmmAlQura') methodIdx = 2;
          await _dio.get('/api/prayer/times', queryParameters: {'method': methodIdx});
        }
        success = true;
      }
      return success;
    } catch (e) {
      debugPrint('Error saving prayer config: $e');
      return false;
    }
  }

  // Get current prayer configuration
  Future<Map<String, dynamic>> getPrayerConfig() async {
    try {
      final response = await _dio.get('/api/prayer/config');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting prayer config: $e');
      return {};
    }
  }

  // Fetch CSV status
  Future<Map<String, dynamic>> getCsvStatus(int year) async {
    try {
      final response = await _dio.get('/api/csv/status', queryParameters: {'year': year});
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting CSV status: $e');
      return {};
    }
  }

  // Toggle CSV mode
  Future<bool> toggleCsvMode(bool enabled) async {
    try {
      final response = await _dio.post(
        '/api/csv/toggle',
        queryParameters: {'enabled': enabled ? 1 : 0},
        data: 'enabled=${enabled ? 1 : 0}',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && (data['ok'] == true || data['status'] == 'ok');
    } catch (e) {
      debugPrint('Error toggling CSV mode: $e');
      return false;
    }
  }

  // Toggle Calendar Only
  Future<bool> toggleCalendarOnly(bool enabled) async {
    try {
      final response = await _dio.post(
        '/api/calendar/only',
        queryParameters: {'enabled': enabled ? 1 : 0},
        data: 'enabled=${enabled ? 1 : 0}',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && (data['ok'] == true || data['status'] == 'ok');
    } catch (e) {
      debugPrint('Error toggling calendar only: $e');
      return false;
    }
  }

  // Toggle Calendar Fallback
  Future<bool> toggleCalendarFallback(bool enabled) async {
    try {
      final response = await _dio.post(
        '/api/calendar/fallback',
        queryParameters: {'enabled': enabled ? 1 : 0},
        data: 'enabled=${enabled ? 1 : 0}',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && (data['ok'] == true || data['status'] == 'ok');
    } catch (e) {
      debugPrint('Error toggling calendar fallback: $e');
      return false;
    }
  }

  // Fetch Calendar Month CSV content
  Future<Map<String, dynamic>> getCalendarMonth(int year, int month) async {
    try {
      final response = await _dio.get('/api/calendar/month', queryParameters: {'year': year, 'month': month});
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting calendar month: $e');
      return {};
    }
  }

  // Save Calendar Month CSV content
  Future<bool> saveCalendarMonth(int year, int month, String csv) async {
    try {
      final response = await _dio.post(
        '/api/calendar/month',
        data: 'year=$year&month=$month&csv=${Uri.encodeComponent(csv)}',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving calendar month: $e');
      return false;
    }
  }

  // Download Calendar Month from server
  Future<Map<String, dynamic>> downloadCalendarMonth(int year, int month, String country, String city, int method, {bool force = false}) async {
    try {
      final response = await _dio.post(
        '/api/calendar/download_month',
        data: 'year=$year&month=$month&country=${Uri.encodeComponent(country)}&city=${Uri.encodeComponent(city)}&method=$method&force=${force ? 1 : 0}',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error downloading calendar month: $e');
      return {};
    }
  }

  // Fetch calendar download status
  Future<Map<String, dynamic>> getCalendarDownloadStatus() async {
    try {
      final response = await _dio.get('/api/calendar/download_status');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting calendar download status: $e');
      return {};
    }
  }

  // Delete Year Calendar from SD
  Future<bool> deleteYearCalendar(int year) async {
    try {
      final response = await _dio.post(
        '/api/calendar/delete_year',
        data: 'year=$year',
        options: Options(headers: {'Content-Type': 'application/x-www-form-urlencoded'}),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting year calendar: $e');
      return false;
    }
  }

  // Upload custom CSV file for a month
  Future<bool> uploadCsvMonth(int month, List<int> bytes, String filename) async {
    try {
      final formData = FormData.fromMap({
        'file': MultipartFile.fromBytes(bytes, filename: filename),
      });
      final response = await _dio.post(
        '/api/csv/upload',
        data: formData,
        queryParameters: {'month': month},
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error uploading CSV month: $e');
      return false;
    }
  }

  // Get countries list
  Future<List<String>> getCountries() async {
    try {
      final response = await _dio.get('/api/location/countries');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data != null && data['countries'] is List) {
        return List<String>.from(data['countries']);
      }
      return [];
    } catch (e) {
      debugPrint('Error getting countries: $e');
      return [];
    }
  }

  // Get cities list for country
  Future<List<String>> getCities(String country) async {
    try {
      final response = await _dio.get('/api/location/cities', queryParameters: {'country': country});
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data != null && data['cities'] is List) {
        return List<String>.from(data['cities']);
      }
      return [];
    } catch (e) {
      debugPrint('Error getting cities: $e');
      return [];
    }
  }

  // Save location (country/city) to device
  Future<bool> saveLocation(String country, String city) async {
    try {
      final response = await _dio.get(
        '/api/prayer/times',
        queryParameters: {'country': country, 'city': city},
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && (data['ok'] == true || data['valid'] == true);
    } catch (e) {
      debugPrint('Error saving location: $e');
      return false;
    }
  }

  // --- Daily Offset APIs ---
  Future<Map<String, dynamic>> getDailyOffset(String date) async {
    try {
      final response = await _dio.get('/api/prayer/daily_offset', queryParameters: {'date': date});
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting daily offset: $e');
      return {'ok': false, 'error': e.toString()};
    }
  }

  Future<bool> saveDailyOffset(String date, int fajr, int dhuhr, int asr, int maghrib, int isha) async {
    try {
      final response = await _dio.post(
        '/api/prayer/daily_offset',
        data: 'date=${Uri.encodeComponent(date)}&fajr=$fajr&dhuhr=$dhuhr&asr=$asr&maghrib=$maghrib&isha=$isha',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving daily offset: $e');
      return false;
    }
  }

  Future<bool> deleteDailyOffset(String date) async {
    try {
      final response = await _dio.post(
        '/api/prayer/daily_offset/delete',
        data: 'date=${Uri.encodeComponent(date)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting daily offset: $e');
      return false;
    }
  }

  // --- Startup Sound Settings APIs ---
  Future<Map<String, dynamic>> getStartupStatus() async {
    try {
      final response = await _dio.get('/api/startup/status');
      return response.data is String ? jsonDecode(response.data) : response.data;
    } catch (e) {
      debugPrint('Error getting startup status: $e');
      return {'enabled': false, 'file': ''};
    }
  }

  Future<bool> saveStartupStatus(bool enabled, String file) async {
    try {
      final response = await _dio.post(
        '/api/startup/save',
        data: 'enabled=${enabled ? 1 : 0}&file=${Uri.encodeComponent(file)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving startup status: $e');
      return false;
    }
  }

  // --- Session Timeout APIs ---
  Future<int> getSessionTimeout() async {
    try {
      final response = await _dio.get('/api/session/timeout');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['timeout'] ?? 10;
    } catch (e) {
      debugPrint('Error getting session timeout: $e');
      return 10;
    }
  }

  Future<bool> saveSessionTimeout(int timeout) async {
    try {
      final response = await _dio.post(
        '/api/session/timeout/save',
        data: 'timeout=$timeout',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving session timeout: $e');
      return false;
    }
  }

  // --- Presence & Smart Welcome APIs ---
  Future<Map<String, dynamic>?> getPresenceConfig() async {
    try {
      final response = await _dio.get('/api/presence/config');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is Map<String, dynamic> ? data : (data as Map?)?.cast<String, dynamic>();
    } catch (e) {
      debugPrint('Error getting presence config: $e');
      return null;
    }
  }

  Future<bool> savePresenceConfig(Map<String, dynamic> cfg) async {
    try {
      final queryParams = cfg.map((key, value) => MapEntry(key, value.toString()));
      final body = queryParams.entries.map((e) => '${Uri.encodeComponent(e.key)}=${Uri.encodeComponent(e.value)}').join('&');
      final response = await _dio.post(
        '/api/presence/config',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving presence config: $e');
      return false;
    }
  }

  Future<Map<String, dynamic>?> getPresenceStatus() async {
    try {
      final response = await _dio.get('/api/presence/status');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data is Map<String, dynamic> ? data : (data as Map?)?.cast<String, dynamic>();
    } catch (e) {
      debugPrint('Error getting presence status: $e');
      return null;
    }
  }

  Future<List<dynamic>> scanPresenceWifi() async {
    try {
      final response = await _dio.get('/api/presence/scan-wifi');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data is Map && data.containsKey('devices') && data['devices'] is List) {
        return List<dynamic>.from(data['devices']);
      } else if (data is List) {
        return data;
      }
      return [];
    } catch (e) {
      debugPrint('Error scanning presence wifi devices: $e');
      return [];
    }
  }

  Future<List<dynamic>> scanPresenceBle({bool start = false}) async {
    try {
      if (start) {
        await _dio.get('/api/presence/scan-ble', queryParameters: {'start': 1});
        await Future.delayed(const Duration(milliseconds: 2300));
      }
      final response = await _dio.get('/api/presence/scan-ble');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      if (data is Map && data.containsKey('devices') && data['devices'] is List) {
        return List<dynamic>.from(data['devices']);
      } else if (data is List) {
        return data;
      }
      return [];
    } catch (e) {
      debugPrint('Error scanning presence ble devices: $e');
      return [];
    }
  }

  Future<bool> savePresenceDevice({
    int id = -1,
    required String name,
    required String mac,
    String welcomeFile = '/Azan/welcome.mp3',
    bool enabled = true,
  }) async {
    try {
      final body = 'id=$id&name=${Uri.encodeComponent(name)}&mac=${Uri.encodeComponent(mac)}&welcomeFile=${Uri.encodeComponent(welcomeFile)}&enabled=${enabled ? "1" : "0"}';
      final response = await _dio.post(
        '/api/presence/device/save',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving presence device: $e');
      return false;
    }
  }

  Future<bool> deletePresenceDevice(int id) async {
    try {
      final response = await _dio.post(
        '/api/presence/device/delete',
        data: 'id=$id',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && data['ok'] == true;
    } catch (e) {
      debugPrint('Error deleting presence device: $e');
      return false;
    }
  }

  Future<bool> testPresenceWelcome({String file = '', int volume = 0}) async {
    try {
      final body = 'file=${Uri.encodeComponent(file)}&volume=$volume';
      final response = await _dio.post(
        '/api/presence/test',
        data: body,
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data != null && data['ok'] == true;
    } catch (e) {
      debugPrint('Error testing presence welcome: $e');
      return false;
    }
  }

  // --- Setup AP (Emergency Hotspot) APIs ---
  Future<Map<String, dynamic>> getApConfig() async {
    try {
      final response = await _dio.get('/api/ap/config');
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return Map<String, dynamic>.from(data ?? {});
    } catch (e) {
      debugPrint('Error getting AP config: $e');
      return {};
    }
  }

  Future<bool> saveApConfig({required String ssid, String pass = ''}) async {
    try {
      final response = await _dio.post(
        '/api/ap/config',
        data: 'ssid=${Uri.encodeComponent(ssid)}&pass=${Uri.encodeComponent(pass)}',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error saving AP config: $e');
      return false;
    }
  }

  // --- Browser/Phone Clock Auto-Sync API ---
  Future<bool> syncBrowserTime([int? epochSeconds]) async {
    try {
      final timestamp = epochSeconds ?? (DateTime.now().millisecondsSinceEpoch ~/ 1000);
      final response = await _dio.post(
        '/api/time/sync_browser',
        data: 'timestamp=$timestamp',
        options: Options(
          headers: {
            'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8',
          },
        ),
      );
      final data = response.data is String ? jsonDecode(response.data) : response.data;
      return data['ok'] == true;
    } catch (e) {
      debugPrint('Error syncing browser time: $e');
      return false;
    }
  }
}


