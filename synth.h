/* Геометрия экрана 960x544. Все размеры в пикселях экрана. */
#ifndef LAYOUT_H
#define LAYOUT_H

#define SCR_W 960
#define SCR_H 544

/* корпус */
#define BODY_X 4
#define BODY_Y 4
#define BODY_W 952
#define BODY_H 536
#define BODY_R 24

/* верхняя полоса: логотип, капсула (MODE | OCTAVE), табло аккорда, отладка */
#define CAP_X 238
#define CAP_Y 10
#define CAP_W 378
#define CAP_H 48
#define RADIO_STRUM_X 266     /* центр светодиода */
#define RADIO_PAD_X 350
#define RADIO_Y 44
#define OCT_X 452             /* "пилюля" октавы */
#define OCT_Y 31
#define OCT_W 152
#define OCT_H 24
#define LCD_X 626             /* табло аккорда */
#define LCD_Y 12
#define LCD_W 216
#define LCD_H 44

/* колесо аккордов */
#define WHEEL_CX 250
#define WHEEL_CY 300
#define BEZEL_R 238           /* кремовый ободок */
#define DISC_R 226            /* синий диск */
#define ROOT_R0 152           /* сегменты тоник: внутренний и внешний радиус */
#define ROOT_R1 220
#define SEP_R0 141            /* кремовое кольцо-разделитель */
#define SEP_R1 147
#define MOD_R0 68             /* сегменты модификаторов */
#define MOD_R1 134
#define CENTER_R 58           /* середина: сброс аккорда */
#define SEG_GAP 5.0f          /* зазор между сегментами */

/* струнная панель */
#define PLATE_X 476
#define PLATE_Y 66
#define PLATE_W 472
#define PLATE_H 468
#define PLATE_CARVE_R 250     /* вогнутый левый край по окружности вокруг колеса */

#define STR_TOP 80            /* струны (полосы справа): верх и низ области */
#define STR_BOTTOM 520
#define BAR_H 13
#define KB_TOP 100            /* клавиатура слева сжата по высоте */
#define KB_BOTTOM 500

#define BK_X0 508             /* чёрная клавиша */
#define BK_X1 570
#define BK_H 11
#define WK_X0 528             /* белая: задняя часть (между чёрными) */
#define WK_XF 570             /* белая: передняя часть во всю ширину */
#define WK_X1 612
#define NECK_X1 644           /* сужение в нить */
#define ROUTE_X1 722          /* S-переход к своей струне */
#define BULB_X1 750           /* расширение в полосу */
#define BAR_X1 900
#define LINE_T 2.4f           /* толщина нити */
#define LABEL_X 908

#endif
