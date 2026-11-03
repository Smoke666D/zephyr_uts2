from collections import deque


class MovingAverageFilter:

  def __init__(self, window_size=5):
    # Очередь с фиксированной длиной (ровно N последних выборок)
    self.window_size = window_size
    self.buffer = deque(maxlen=window_size)

  def update(self, new_val):
    self.buffer.append(new_val)
    # Возвращаем среднее арифметическое по последним выборкам
    return sum(self.buffer) / len(self.buffer)

  def reset(self):
    self.buffer.clear()