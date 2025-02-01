#ifndef gps_CONTROLLER_H
#define gps_CONTROLLER_H

#ifdef __cplusplus
extern "C" {
#endif
void gps_controller_init();
extern float f_speed_gps ;
extern uint8_t total_sats;
extern double f_latitude_lv ;
extern double f_longitude_lv ;

#ifdef __cplusplus
}
#endif
#endif //gps_CONTROLLER_H
