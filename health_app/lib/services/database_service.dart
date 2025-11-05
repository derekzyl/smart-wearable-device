import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';
import 'package:path_provider/path_provider.dart';
import 'dart:io';
import '../models/vitals_data.dart';

class DatabaseService {
  static final DatabaseService _instance = DatabaseService._internal();
  factory DatabaseService() => _instance;
  DatabaseService._internal();

  Database? _database;

  Future<Database> get database async {
    if (_database != null) return _database!;
    _database = await _initDatabase();
    return _database!;
  }

  Future<Database> _initDatabase() async {
    Directory documentsDirectory = await getApplicationDocumentsDirectory();
    String path = join(documentsDirectory.path, 'health_watch.db');
    return await openDatabase(
      path,
      version: 1,
      onCreate: _onCreate,
    );
  }

  Future<void> _onCreate(Database db, int version) async {
    await db.execute('''
      CREATE TABLE vitals(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        heart_rate REAL,
        spo2 REAL,
        temperature REAL,
        steps INTEGER,
        battery_level REAL,
        timestamp TEXT
      )
    ''');

    await db.execute('''
      CREATE TABLE imu_data(
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        accel_x REAL,
        accel_y REAL,
        accel_z REAL,
        gyro_x REAL,
        gyro_y REAL,
        gyro_z REAL,
        steps INTEGER,
        timestamp TEXT
      )
    ''');
  }

  Future<void> init() async {
    await database;
  }

  Future<int> insertVitals(VitalsData vitals) async {
    final db = await database;
    return await db.insert('vitals', vitals.toJson());
  }

  Future<List<VitalsData>> getVitals(DateTime start, DateTime end) async {
    final db = await database;
    final List<Map<String, dynamic>> maps = await db.query(
      'vitals',
      where: 'timestamp >= ? AND timestamp <= ?',
      whereArgs: [start.toIso8601String(), end.toIso8601String()],
      orderBy: 'timestamp DESC',
    );
    return List.generate(maps.length, (i) => VitalsData.fromJson(maps[i]));
  }

  Future<void> deleteOldVitals(int daysOld) async {
    final db = await database;
    final cutoffDate = DateTime.now().subtract(Duration(days: daysOld));
    await db.delete(
      'vitals',
      where: 'timestamp < ?',
      whereArgs: [cutoffDate.toIso8601String()],
    );
  }
}
