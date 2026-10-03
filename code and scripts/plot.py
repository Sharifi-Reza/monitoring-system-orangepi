import matplotlib.pyplot as plt

# زمان بر اساس i * 30 (ثانیه)
time = [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300]

# داده‌های استخراج شده
temp_2_1_idle = [46.4, 48.0, 47.4, 45.6, 48.0, 49.1, 45.8, 45.6, 49.1, 49.4, 45.3]
temp_2_2_stream = [48.5, 47.5, 50.5, 51.2, 48.8, 50.0, 52.4, 52.2, 50.6, 51.3, 53.5]
temp_2_3 = [45.6, 46.8, 48.2, 50.0, 50.9, 51.8, 52.3, 53.5, 54.1, 55.1, 55.1]

# تنظیمات نمودار
plt.figure(figsize=(10, 6))

# رسم خطوط برای هر تست
plt.plot(time, temp_2_1_idle, marker='o', linestyle='-', color='green', label='2-1.PNG (Idle)')
plt.plot(time, temp_2_2_stream, marker='s', linestyle='-', color='blue', label='2-2.PNG (Stream)')
plt.plot(time, temp_2_3, marker='^', linestyle='-', color='red', label='2-3.PNG')

# برچسب‌ها و عنوان
plt.title('Temperature over Time (OrangePi Zero Plus 2 H5)')
plt.xlabel('Time (Seconds)')
plt.ylabel('Temperature (°C)')

# نمایش گرید و راهنمای نمودار
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend()

# نمایش نمودار
plt.tight_layout()
plt.show()
import matplotlib.pyplot as plt

# تولید داده‌های زمان: 61 نمونه (0 تا 60) با فواصل 5 ثانیه
# خروجی: [0, 5, 10, 15, ..., 300]
time = [i * 5 for i in range(61)]

# داده‌های حافظه: 61 نمونه که همگی برابر با 4408 هستند
memory = [4408 for _ in range(61)]

# ایجاد قاب نمودار
plt.figure(figsize=(10, 6))

# رسم خط ثابت حافظه
plt.plot(time, memory, linestyle='-', color='purple', linewidth=2, label='Webserver Memory (4408 KB)')

# تنظیم محدوده محور Y برای نمایش بهتر خط افقی
plt.ylim(4000, 5000)

# تنظیمات برچسب‌ها و عنوان
plt.title('Memory Usage over Time (Webserver Process)')
plt.xlabel('Time (Seconds)')
plt.ylabel('Memory (KB)')

# فعال‌سازی خطوط شبکه (Grid) و راهنما (Legend)
plt.grid(True, linestyle='--', alpha=0.7)
plt.legend()

# نمایش نمودار
plt.tight_layout()
plt.show()