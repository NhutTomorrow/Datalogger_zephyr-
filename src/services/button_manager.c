/**
 * @file button_if.c
 * @brief Hiện thực bộ xử lý chống rung phím (Debounce) và phát sinh sự kiện nút bấm.
 */

#include "button_manager.h"
#include <string.h>

void button_manager_init(button_manager_t *mgr,
                         const button_driver_ops_t *ops,
                         uint8_t button_count,
                         button_event_handler_t event_cb,
                         void *user_data)
{
    if (mgr == NULL)
        return;

    memset(mgr, 0, sizeof(button_manager_t));
    mgr->ops = ops;
    mgr->button_count = (button_count > BUTTON_MAX_COUNT) ? BUTTON_MAX_COUNT : button_count;
    mgr->event_cb = event_cb;
    mgr->user_data = user_data;

    if (ops != NULL && ops->init != NULL)
    {
        ops->init();
    }
}

void button_manager_tick(button_manager_t *mgr, uint32_t current_tick_ms)
{
    if (mgr == NULL || mgr->ops == NULL || mgr->ops->read_raw_state == NULL)
    {
        return;
    }
    if (mgr->ops->button_scan != NULL)
    {
        mgr->ops->button_scan();
    }
    for (uint8_t i = 0; i < mgr->button_count; i++)
    {
        bool raw_pressed = mgr->ops->read_raw_state(i);
        button_state_t *st = &mgr->states[i];

        /* Phát hiện cạnh xuống (bắt đầu nhấn) */
        if (raw_pressed && !st->prev_state)
        {
            st->press_start_tick = current_tick_ms;
            st->long_press_fired = false;
        }
        /* Đang giữ nút */
        else if (raw_pressed && st->prev_state)
        {
            uint32_t elapsed = current_tick_ms - st->press_start_tick;
            if (elapsed >= BUTTON_LONG_PRESS_TIME_MS && !st->long_press_fired)
            {
                st->long_press_fired = true;
                if (mgr->event_cb != NULL)
                {
                    mgr->event_cb((button_id_t)i, BUTTON_EVT_LONG_PRESS, mgr->user_data);
                }
            }
        }
        /* Phát hiện cạnh lên (nhả nút) */
        else if (!raw_pressed && st->prev_state)
        {
            uint32_t elapsed = current_tick_ms - st->press_start_tick;
            /* Nếu thời gian nhấn vượt qua ngưỡng debounce và chưa bắn long press -> bắn CLICK */
            if (elapsed >= BUTTON_DEBOUNCE_TIME_MS && !st->long_press_fired)
            {
                if (mgr->event_cb != NULL)
                {
                    mgr->event_cb((button_id_t)i, BUTTON_EVT_CLICK, mgr->user_data);
                }
            }
        }

        st->prev_state = raw_pressed;
    }
}
