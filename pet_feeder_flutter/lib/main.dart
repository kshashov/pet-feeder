import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  runApp(MyApp());
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'CHAIKA Pet Feeder',
      theme: ThemeData(primarySwatch: Colors.blue, useMaterial3: true),
      home: AlarmConfigPage(),
    );
  }
}

class AlarmConfigPage extends StatefulWidget {
  @override
  _AlarmConfigPageState createState() => _AlarmConfigPageState();
}

class _AlarmConfigPageState extends State<AlarmConfigPage> {
  // --- Конфигурация API ---
  final String addEndpoint = '/alarms';
  final String patchEndpoint = '/alarms';
  final String removeEndpoint = '/alarms';
  final String getEndpoint = '/alarms';
  final String feedEndpoint = '/alarms/feed';
  final int port = 80;

  // Список будильников: хранит только секунды дня (int)
  List<Map<String, Object?>> alarms = [];

  // Поле ввода URL/IP
  final TextEditingController _urlController = TextEditingController();
  String _currentUrl = '192.168.1.33'; // URL по умолчанию

  // Состояние
  bool _loading = false;

  @override
  void initState() {
    super.initState();
    _loadSavedUrl().then((_) {
      _urlController.text = _currentUrl;
      _loadAlarmsFromESP32();
    });
  }

  // --- Управление URL/IP ---

  Future<void> _loadSavedUrl() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _currentUrl = prefs.getString('esp32_url') ?? '192.168.1.33';
    });
  }

  Future<void> _saveUrl() async {
    final url = _urlController.text.trim();
    if (url.isEmpty) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('❌ URL Should not be empty')));
      return;
    }

    setState(() {
      _currentUrl = url;
    });

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('esp32_url', url);

    ScaffoldMessenger.of(
      context,
    ).showSnackBar(SnackBar(content: Text('✅ URL is saved: $_currentUrl')));
  }

  // --- API Взаимодействие ---

  // Загрузить будильники (GET)
  Future<void> _loadAlarmsFromESP32() async {
    setState(() {
      _loading = true;
    });

    final url = Uri.http('$_currentUrl:$port', getEndpoint);

    try {
      final response = await http.get(url);

      if (response.statusCode == 200) {
        final data = jsonDecode(response.body) as List;

        // Список состоит из целых чисел (секунд)
        final List<Map<String, Object?>> newAlarms = data
            .map((item) => (item as Map<String, Object?>))
            .toList();

        setState(() {
          alarms = newAlarms;
        });

        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('✅ Loaded ${newAlarms.length} items')),
        );
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              'Loading Error: ${response.statusCode} - ${response.body}',
            ),
          ),
        );
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Connection Error: $e')));
    } finally {
      setState(() {
        _loading = false;
      });
    }
  }

  // Добавить новый будильник (PUT)
  Future<void> _addAlarm() async {
    final selectedTime = await showTimePicker(
      context: context,
      initialTime: TimeOfDay.now(),
    );
    if (selectedTime == null) return;

    final int? size = await showDialog<int>(
      context: context,
      builder: (context) => SizeInputDialog(),
    );

    final secondsOfDay = selectedTime.hour * 3600 + selectedTime.minute * 60;

    final url = Uri.http('$_currentUrl:$port', addEndpoint);

    try {
      // Отправляем JSON-тело с ключом 'time'
      final body = jsonEncode(
        {'time': secondsOfDay, 'size': size} as Map<String, Object?>,
      );

      final response = await http.put(
        url,
        headers: {'Content-Type': 'application/json'},
        body: body,
      );

      if (response.statusCode == 200) {
        // Перезагружаем список
        await _loadAlarmsFromESP32();
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('✅ Added')));
      } else {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error: ${response.body}')));
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Error: $e')));
    }
  }

  // Изменить будильник (PATCH)
  Future<void> _patchAlarm(int seconds, int size) async {
    final int? newSize = await showDialog<int>(
      context: context,
      builder: (context) => SizeInputDialog(initialValue: size),
    );
    if (newSize == null ||  newSize == size) {
      return;
    }

    final url = Uri.http('$_currentUrl:$port', patchEndpoint);

    try {
      // Отправляем JSON-тело с ключом 'time'
      final body = jsonEncode(
        {'time': seconds, 'size': newSize} as Map<String, Object?>,
      );

      final response = await http.patch(
        url,
        headers: {'Content-Type': 'application/json'},
        body: body,
      );

      if (response.statusCode == 200) {
        // Перезагружаем список
        await _loadAlarmsFromESP32();
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('✅ Updated')));
      } else {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error: ${response.body}')));
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Error: $e')));
    }
  }

  // Удалить будильник (DELETE)
  Future<void> _removeAlarm(int secondsOfDay) async {
    // Отправляем секунды в Query Parameter
    final url = Uri.http('$_currentUrl:$port', removeEndpoint, {
      'time': secondsOfDay.toString(),
    });

    try {
      final response = await http.delete(url);

      if (response.statusCode == 200) {
        // Перезагружаем список
        await _loadAlarmsFromESP32();
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('✅ Deleted')));
      } else {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error: ${response.body}')));
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Error: $e')));
    }
  }

  Future<void> _runAlarm(int secondsOfDay) async {
    // Отправляем секунды в Query Parameter
    final url = Uri.http('$_currentUrl:$port', feedEndpoint, {
      'time': secondsOfDay.toString(),
    });

    try {
      final response = await http.post(url);

      if (response.statusCode == 200) {
        // Перезагружаем список
        await _loadAlarmsFromESP32();
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('✅ Feeded')));
      } else {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error: ${response.body}')));
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Error: $e')));
    }
  }

  Future<void> _feedNow() async {
    // Отправляем секунды в Query Parameter
    final url = Uri.http('$_currentUrl:$port', feedEndpoint);

    try {
      final response = await http.post(url);

      if (response.statusCode == 200) {
        // Перезагружаем список
        await _loadAlarmsFromESP32();
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Feeded')));
      } else {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(SnackBar(content: Text('Error: ${response.body}')));
      }
    } catch (e) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Error: $e')));
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('CHAIKA Pet Feeder'),
        centerTitle: true,
        actions: [
          IconButton(
            icon: Icon(Icons.refresh),
            tooltip: 'Refresh',
            onPressed: _loading ? null : _loadAlarmsFromESP32,
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          children: [
            // === Поле ввода URL ===
            Row(
              children: [
                Expanded(
                  child: TextField(
                    controller: _urlController,
                    keyboardType: TextInputType.url,
                    decoration: InputDecoration(
                      labelText: 'URL',
                      hintText: 'esp32-alarm.local или 192.168.1.33',
                      border: OutlineInputBorder(),
                    ),
                  ),
                ),
                SizedBox(width: 8),
                ElevatedButton(onPressed: _saveUrl, child: Icon(Icons.save)),
              ],
            ),
            SizedBox(height: 16),

            // Текущий адрес
            Text(
              'Feeder: http://$_currentUrl:$port',
              style: TextStyle(fontSize: 14, color: Colors.grey),
            ),
            SizedBox(height: 16),

            Row(
              children: [
                // Кнопка добавления (отправляет PUT)
                ElevatedButton.icon(
                  onPressed: _addAlarm,
                  icon: Icon(Icons.add, size: 16),
                  label: Text('Plan New'),
                ),
                SizedBox(width: 16),
                ElevatedButton.icon(
                  onPressed: _feedNow,
                  icon: Icon(Icons.alarm, size: 16),
                  label: Text('Feed Now'),
                ),
              ],
            ),

            SizedBox(height: 16),

            // Индикатор загрузки
            if (_loading)
              LinearProgressIndicator(minHeight: 2)
            else
              SizedBox(height: 2),

            // Список будильников
            Expanded(
              child: alarms.isEmpty
                  ? Center(child: Text('Нет будильников'))
                  : ListView.builder(
                      itemCount: alarms.length,
                      itemBuilder: (context, index) {
                        final seconds = alarms[index]['time'] as int;
                        final size = alarms[index]['size'] as int;
                        final h = seconds ~/ 3600;
                        final m = (seconds % 3600) ~/ 60;
                        final formatted =
                            '${h.toString().padLeft(2, '0')}:${m.toString().padLeft(2, '0')}';

                        return Card(
                          child: ListTile(
                            title: Row(
                              // spacing: 10,
                              children: [
                                Text(formatted, style: TextStyle(fontSize: 18)),
                                // Text(
                                // ' ×',
                                // style: TextStyle(fontSize: 18, color: Colors.deepPurple)),
                                Text(
                                  ' ×${size}',
                                  style: TextStyle(
                                    fontSize: 18,
                                    color: Theme.of(context).primaryColor,
                                  ),
                                ),
                              ],
                            ),
                            subtitle: Row(
                              spacing: 10,
                              children: [
                                (alarms[index]['alarmed'] as bool)
                                    ? Text(
                                        'Done',
                                        style: TextStyle(color: Colors.green),
                                      )
                                    : Text("Waiting"),
                              ],
                            ),
                            trailing: Row(
                              mainAxisSize: MainAxisSize.min,
                              children: [
                                IconButton(
                                  icon: Icon(
                                    Icons.play_arrow,
                                    color: Colors.green,
                                  ),
                                  onPressed: () => _runAlarm(seconds),
                                ),
                                IconButton(
                                  icon: Icon(Icons.edit, color: Colors.blue),
                                  onPressed: () => _patchAlarm(seconds, size),
                                ),
                                IconButton(
                                  icon: Icon(Icons.delete, color: Colors.red),
                                  onPressed: () => _removeAlarm(seconds),
                                ),
                              ],
                            ),
                          ),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}

class SizeInputDialog extends StatefulWidget {
  final int initialValue;

  SizeInputDialog({this.initialValue = 1});

  @override
  _SizeInputDialogState createState() => _SizeInputDialogState();
}

class _SizeInputDialogState extends State<SizeInputDialog> {
  late int _currentSize;

  @override
  void initState() {
    super.initState();
    _currentSize = widget.initialValue;
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: Text('Amount'),
      content: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text('How much (1-10)?'),
          SizedBox(height: 10),
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              IconButton(
                icon: Icon(Icons.remove),
                onPressed: () {
                  if (_currentSize > 1) {
                    setState(() => _currentSize--);
                  }
                },
              ),
              Text(
                _currentSize.toString(),
                style: TextStyle(fontSize: 24, fontWeight: FontWeight.bold),
              ),
              IconButton(
                icon: Icon(Icons.add),
                onPressed: () {
                  if (_currentSize < 10) {
                    setState(() => _currentSize++);
                  }
                },
              ),
            ],
          ),
        ],
      ),
      actions: [
        TextButton(
          child: Text('Отмена'),
          onPressed: () => Navigator.of(context).pop(),
        ),
        ElevatedButton(
          child: Text('OK'),
          onPressed: () => Navigator.of(context).pop(_currentSize),
        ),
      ],
    );
  }
}
