import glob
import os
import matplotlib.pyplot as plt
import pandas as pd

# 1. Находим последний созданный CSV-файл в папке logs/
list_of_files = glob.glob('logs/*.csv')
if not list_of_files:
  print('Файлы логов не найдены в папке logs/')
  exit()

latest_file = max(list_of_files, key=os.path.getctime)
print(f'Открываем последний лог: {latest_file}')

# 2. Загружаем данные через Pandas
df = pd.read_csv(latest_file)

# Превращаем колонку Timestamp в формат даты/времени для красивой оси X
df['Timestamp'] = pd.to_datetime(df['Timestamp'])

# 3. Строим график (например, RF Power и Temperature MU)
plt.figure(figsize=(12, 6))

# Выбираем колонки, которые хочешь нарисовать (названия соответствуют шапке CSV)
if 'RF_Power' in df.columns:
  plt.plot(df['Timestamp'], df['RF_Power'], label='RF Power (дБ)', color='blue')
if 'Temp_MU' in df.columns:
  plt.plot(
      df['Timestamp'], df['Temp_MU'], label='Temp MU (°C)', color='red'
  )
if 'AO_1' in df.columns:
  plt.plot(df['Timestamp'], df['AO_1'], label='AO 1 (В)', color='green')

plt.title(f'Лог телеметрии: {os.path.basename(latest_file)}')
plt.xlabel('Время')
plt.ylabel('Значения')
plt.legend()
plt.grid(True)
plt.tight_layout()

# Показываем график в окне
plt.show()