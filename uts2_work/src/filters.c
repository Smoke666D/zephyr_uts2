#include "filters.h"

 uint16_t apply_median_filter_5(channel_median_filter_t *filter, uint16_t new_val)
{
    uint16_t sorted[MEDIAN_WINDOW_SIZE];

    // 1. Кольцевой буфер
    filter->history[filter->head] = new_val;
    filter->head = (filter->head + 1) % MEDIAN_WINDOW_SIZE;

    // 2. Копируем для сортировки
    memcpy(sorted, filter->history, sizeof(sorted));

   
    // 3. Сортировка вставкой (абсолютно надежная для любого размера окна)
    for (int i = 1; i < MEDIAN_WINDOW_SIZE; i++) 
    {
    
    
       uint16_t key = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
  
    // 4. Возвращаем точную медиану (середину отсортированного массива)
    return sorted[MEDIAN_WINDOW_SIZE / 2];
}