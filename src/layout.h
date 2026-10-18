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

/* верхняя полоса: логотип, капсула (MODE | OCTAVE | TRANSPOSE), табло аккорда, отладка */
#define CAP_X 238
#define CAP_Y 10
#define CAP_W 378
#define CAP_H 48
#define RADIO_STRUM_X 262     /* центры светодиодов режимов */
#define RADIO_PAD_X 326
#define RADIO_GTR_X 374
#define RADIO_Y 44
#define OCT_X 452             /* "пилюля" октавы */
#define OCT_Y 31
#define OCT_W 70
#define TRN_X 534             /* "пилюля" транспозиции (та же высота) */
#define TRN_W 70
#define OCT_H 24
#define LCD_X 626             /* табло аккорда */
#define LCD_Y 12
#define LCD_W 216
#define LCD_H 44

/* Pad only: на месте струн 4 фейдера (громкость, атака, срез фильтра, модуляция; 100% сверху)
 * и внизу кнопка HOLD */
#define FD_N 4
#define FD_X0 (PLATE_X + 72)  /* центр первого фейдера */
#define FD_DX 110
#define FD_Y0 (PLATE_Y + 96)
#define FD_Y1 (PLATE_Y + PLATE_H - 92)
#define FD_GW 22              /* ширина жёлоба */
#define FD_KW 84              /* ручка */
#define FD_KH 40
#define HOLD_CX (PLATE_X + PLATE_W / 2)
#define HOLD_CY (PLATE_Y + PLATE_H - 40)
#define HOLD_W 200
#define HOLD_H 44

/* страница настроек (касание логотипа): панель по центру, три строки с галочками */
#define LOGO_X1 232           /* логотип: x < LOGO_X1, y < LOGO_Y1 */
#define LOGO_Y1 62
#define SET_X 210
#define SET_Y 58
#define SET_W 540
#define SET_H 418
#define SET_ROW_Y0 142        /* центр первой строки */
#define SET_ROW_DY 70
#define SET_ROWS 4
#define SET_CHK_X 262         /* центр галочки */
#define SET_CHK 34            /* размер галочки */

/* громкость (справа от табло): в Pad - громкость пэда, в остальных режимах - струн */
#define VOL_X 852
#define VOL_Y 12
#define VOL_W 96
#define VOL_H 44
#define VOL_TX0 866           /* дорожка ручки */
#define VOL_TX1 934
#define VOL_TY 42

/* быстрые аккорды: две крестовины в углах слева от колеса - D-pad сверху, стик снизу */
#define QS_N 8
#define QS_DPAD_CX 54
#define QS_DPAD_CY 104
#define QS_STICK_CX 54
#define QS_STICK_CY 498
#define QS_DX 31              /* смещение боковых "кнопок" крестовины */
#define QS_DY 19
#define QS_W 30
#define QS_H 16

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
#define CENTER_R 58           /* середина: две половины - мажор (сверху) и минор (снизу) */
#define SEG_GAP 5.0f          /* зазор между сегментами */

/* струнная панель */
#define PLATE_X 476
#define PLATE_Y 66
#define PLATE_W 472
#define PLATE_H 468
#define PLATE_CARVE_R 250     /* вогнутый левый край по окружности вокруг колеса */

/* пропорции взяты с первого концепта (он нарисован ровно под экран Vita, в 2 раза крупнее) */
#define STR_TOP 74            /* струны (полосы справа): верх и низ области */
#define STR_BOTTOM 526
#define BAR_H 15              /* полосы почти вплотную */
#define KB_TOP 98             /* клавиатура слева сжата по высоте */
#define KB_BOTTOM 502

#define BK_X0 510             /* чёрная клавиша: выступает влево */
#define BK_X1 566
#define BK_NECK 580           /* сужение чёрной заканчивается до передней части белых */
#define BK_H 11
#define WK_X0 530             /* белая: задняя часть (между чёрными) */
#define WK_XF 580             /* белая: передняя часть во всю ширину */
#define WK_X1 612
#define LINE_CLEAR 3.2f       /* зазор от стыка белых до края белой там, где идёт нить чёрной */
#define NECK_X1 634           /* сужение белой в нить */
#define BULB_X1 768           /* нить S-образно уходит к своей струне и к этой точке становится полосой */
#define BULB_TSTART 0.40f     /* с какой доли пути нить начинает утолщаться */
#define BAR_X1 932            /* подписей нот нет - полосы до самого края */
#define LINE_T 2.2f           /* толщина нити */


/* режим "Гитара": гриф на струнной панели. Как гитару обычно рисуют и видит зритель:
 * головка слева, толстая струна внизу. Проведение вверх = удар вниз (от басов к тонким) */
#define GT_TOP 76             /* область струн по высоте */
#define GT_BOTTOM 482
#define GT_NUT_X0 536         /* порожек */
#define GT_NUT_X1 548
#define GT_STR_X0 540         /* струны начинаются у порожка */
#define GT_STR_X1 930
#define GT_SCALE 624.0f       /* мензура в пикселях: 12-й лад на середине (~860) */
#define GT_FRETS 12
#define GT_MARK_X 522         /* метки "x" / "o" слева от порожка */
#define GT_HOLE_CX 1010.0f    /* розетка резонаторного отверстия справа (видна частью) */
#define GT_HOLE_R 140.0f
/* полоска позиций под грифом: номера ладов 1..9 точно под своими ладами и AUTO справа */
#define GT_POS_Y0 494
#define GT_POS_Y1 526
#define GT_AUTO_X0 872
#define GT_AUTO_X1 936

#endif
