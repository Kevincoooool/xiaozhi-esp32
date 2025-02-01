/*
 * @Author: Kevincoooool
 * @Date: 2023-07-25 11:22:02
 * @Description:
 * @version:
 * @Filename: Do not Edit
 * @LastEditTime: 2023-12-19 18:07:12
 * @FilePath: \audio-s3480800\9.gps_demo\main\gps_controller.c
 */
#include <sys/cdefs.h>
#include <stdio.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include <freertos/task.h>
#include "gps_controller.h"
#include "uart_driver.h"
#include "minmea.h"

#define INDENT_SPACES "  "
#define TAG "GPS"

static int a1 = 1;
static int a2 = 1;
static int a3 = 1;
static char rx_buffer[UART_BUFFER_LENGTH] = {0};
 uint8_t total_sats = 0;
static int speed_gps = 0;
static int latitude = 0;
static int longitude = 0;
static double f_latitude = 0;
static double f_longitude = 0;
float f_speed_gps = 0;

double f_latitude_lv = 0;
double f_longitude_lv = 0;

static int hours = 0;
static int minutes = 0;
static int seconds = 0;

// float f_speed_gps = 0;
// double f_latitude = 0;
// double f_longitude = 0;

// 定义最大行数和每行的最大长度
#define MAX_LINES 100
#define MAX_LINE_LENGTH 256

int splitString(const char *input, const char *delimiter, char **lines)
{
    int lineCount = 0;
    char *copy = strdup(input);
    char *line = strtok(copy, delimiter);

    while (line != NULL && lineCount < MAX_LINES)
    {
        lines[lineCount] = strdup(line);
        lineCount++;
        line = strtok(NULL, delimiter);
    }

    free(copy);
    return lineCount;
}
// 定义常量
#define M_PI 3.1415926
#define WGS84_A 6378137.0
#define WGS84_E2 0.00669437999014
// 辅助函数
double transform_lat(double x, double y)
{
    double lat = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y + 0.2 * sqrt(fabs(x));
    lat += (20.0 * sin(6.0 * x * M_PI) + 20.0 * sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    lat += (20.0 * sin(y * M_PI) + 40.0 * sin(y / 3.0 * M_PI)) * 2.0 / 3.0;
    lat += (160.0 * sin(y / 12.0 * M_PI) + 320 * sin(y * M_PI / 30.0)) * 2.0 / 3.0;
    return lat;
}

double transform_lon(double x, double y)
{
    double lon = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y + 0.1 * sqrt(fabs(x));
    lon += (20.0 * sin(6.0 * x * M_PI) + 20.0 * sin(2.0 * x * M_PI)) * 2.0 / 3.0;
    lon += (20.0 * sin(x * M_PI) + 40.0 * sin(x / 3.0 * M_PI)) * 2.0 / 3.0;
    lon += (150.0 * sin(x / 12.0 * M_PI) + 300.0 * sin(x / 30.0 * M_PI)) * 2.0 / 3.0;
    return lon;
}
// 坐标转换函数
void wgs84_to_gcj02(double *lat, double *lon)
{
    if (*lat == 0.0 || *lon == 0.0)
    {
        return;
    }

    double dlat = transform_lat(*lon - 105.0, *lat - 35.0);
    double dlon = transform_lon(*lon - 105.0, *lat - 35.0);
    double radlat = *lat / 180.0 * M_PI;
    double magic = sin(radlat);
    magic = 1 - WGS84_E2 * magic * magic;
    double sqrtmagic = sqrt(magic);
    dlat = (dlat * 180.0) / ((WGS84_A * (1 - WGS84_E2)) / (magic * sqrtmagic) * M_PI);
    dlon = (dlon * 180.0) / (WGS84_A / sqrtmagic * cos(radlat) * M_PI);
    double mglat = *lat + dlat;
    double mglon = *lon + dlon;
    *lat = mglat;
    *lon = mglon;
}
#define GPS_NOISE_STDDEV 0.005     // GPS 测量噪声的标准差
#define GPS_UPDATE_INTERVAL_MS 500 // GPS 数据更新的时间间隔

// 卡尔曼滤波器结构体
typedef struct
{
    double x;            // 状态估计值
    double P;            // 状态估计协方差
    double Q;            // 过程噪声协方差
    double R;            // 测量噪声协方差
    double K;            // 卡尔曼增益
    double prev_time_ms; // 上一次更新的时间戳
} kalman_filter_t;

// 初始化卡尔曼滤波器
void kalman_filter_init(kalman_filter_t *filter, double initial_x, double initial_P, double process_noise_covariance, double measurement_noise_covariance)
{
    filter->x = initial_x;
    filter->P = initial_P;
    filter->Q = process_noise_covariance;
    filter->R = measurement_noise_covariance;
    filter->prev_time_ms = esp_timer_get_time() / 1000.0;
}

// 更新卡尔曼滤波器
double kalman_filter_update(kalman_filter_t *filter, double measurement)
{
    double current_time_ms = esp_timer_get_time() / 1000.0;
    double dt = current_time_ms - filter->prev_time_ms;

    // 预测步骤
    double x_pred = filter->x;
    double P_pred = filter->P + filter->Q;

    // 更新步骤
    filter->K = P_pred / (P_pred + filter->R);
    filter->x = x_pred + filter->K * (measurement - x_pred);
    filter->P = (1 - filter->K) * P_pred;

    filter->prev_time_ms = current_time_ms;

    return filter->x;
}
/**
 * 任务定时向UI发送数据示例
 */
_Noreturn static void gps_update_task()
{
    // 初始化 GPS 数据和滤波器
    double gps_data = 0.0;
    kalman_filter_t gps_filter1;
    kalman_filter_init(&gps_filter1, 0.0, 1.0, pow(GPS_NOISE_STDDEV, 2), pow(GPS_NOISE_STDDEV, 2));
    kalman_filter_t gps_filter2;
    kalman_filter_init(&gps_filter2, 0.0, 1.0, pow(GPS_NOISE_STDDEV, 2), pow(GPS_NOISE_STDDEV, 2));

    ESP_LOGI(TAG, "gps_update_task ");
    // gpio_pad_select_gpio(GPIO_NUM_40);
    // gpio_set_direction(GPIO_NUM_40, GPIO_MODE_OUTPUT);
    // gpio_set_level(GPIO_NUM_40, 0);
    // vTaskDelay(1000);
    // gpio_set_level(GPIO_NUM_40, 1);
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = 0};

    uart_param_config(UART_NUM_4G, &uart_config);
    uart_set_pin(UART_NUM_4G, TX_PIN_4G, RX_PIN_4G, -1, -1);
    uart_driver_install(UART_NUM_4G, UART_BUFFER_LENGTH, UART_BUFFER_LENGTH, 0, NULL, 0);

    size_t len = 0;
    char *lines[MAX_LINES] = {NULL};
    while (1)
    {
        memset(rx_buffer, 0, UART_BUFFER_LENGTH);
        len = SerialRecv(&rx_buffer, UART_BUFFER_LENGTH, 100 / portTICK_PERIOD_MS);
        if (len)
        {
            // for (size_t i = 0; i < len; i++)
            // {
            //     printf("%x ", rx_buffer[i]);
            // }
            printf("%s", rx_buffer);
            // printf("\n");

            int lineCount = splitString(rx_buffer, "\n", lines);

            for (int i = 0; i < lineCount; i++)
            {
                // printf("%s\n", lines[i]);
                switch (minmea_sentence_id(lines[i], false))
                {
                case MINMEA_SENTENCE_RMC:
                {
                    struct minmea_sentence_rmc frame;
                    if (minmea_parse_rmc(&frame, lines[i]))
                    {
                        // latitude = minmea_rescale(&frame.latitude, 1000);
                        // longitude = minmea_rescale(&frame.longitude, 1000);
                        // speed_gps = minmea_rescale(&frame.speed, 1000);
                        f_latitude = minmea_tocoord(&frame.latitude);
                        f_longitude = minmea_tocoord(&frame.longitude);
                        f_speed_gps = minmea_tocoord(&frame.speed);

                        latitude = (int)minmea_tocoord(&frame.latitude);
                        longitude = (int)minmea_tocoord(&frame.longitude);
                        // speed_gps = (int)minmea_rescale(&frame.speed, 1000);
                        // ESP_LOGI(TAG, "$xxRMC: raw coordinates and speed: (%d/%d,%d/%d) %d/%d",
                        //          frame.latitude.value, frame.latitude.scale,
                        //          frame.longitude.value, frame.longitude.scale,
                        //          frame.speed.value, frame.speed.scale);
                        // ESP_LOGI(TAG, "$xxRMC fixed-point coordinates and speed scaled to three decimal places: (%d,%d) %d",
                        //          minmea_rescale(&frame.latitude, 1000),
                        //          minmea_rescale(&frame.longitude, 1000),
                        //          minmea_rescale(&frame.speed, 1000));
                        // ESP_LOGI(TAG, "$xxRMC floating point degree coordinates and speed: (%f,%f) %f",
                        //          minmea_tocoord(&frame.latitude),
                        //          minmea_tocoord(&frame.longitude),
                        //          minmea_tofloat(&frame.speed));
                        // printf("l :%f  l :%f  s :%f   \n", f_latitude, f_longitude, f_speed_gps);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "$xxRMC sentence is not parsed\n");
                    }
                }
                break;

                case MINMEA_SENTENCE_GGA:
                {
                    struct minmea_sentence_gga frame;
                    if (minmea_parse_gga(&frame, lines[i]))
                    {
                        // ESP_LOGI(TAG, "$xxGGA: fix quality: %d\n", frame.fix_quality);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "$xxGGA sentence is not parsed\n");
                    }
                }
                break;

                case MINMEA_SENTENCE_GST:
                {
                    struct minmea_sentence_gst frame;
                    if (minmea_parse_gst(&frame, lines[i]))
                    {
                        // ESP_LOGI(TAG, "$xxGST: raw latitude,longitude and altitude error deviation: (%d/%d,%d/%d,%d/%d)",
                        //          frame.latitude_error_deviation.value, frame.latitude_error_deviation.scale,
                        //          frame.longitude_error_deviation.value, frame.longitude_error_deviation.scale,
                        //          frame.altitude_error_deviation.value, frame.altitude_error_deviation.scale);
                        // ESP_LOGI(TAG, "$xxGST fixed point latitude,longitude and altitude error deviation"
                        //               " scaled to one decimal place: (%d,%d,%d)",
                        //          minmea_rescale(&frame.latitude_error_deviation, 10),
                        //          minmea_rescale(&frame.longitude_error_deviation, 10),
                        //          minmea_rescale(&frame.altitude_error_deviation, 10));
                        // ESP_LOGI(TAG, "$xxGST floating point degree latitude, longitude and altitude error deviation: (%f,%f,%f)",
                        //          minmea_tofloat(&frame.latitude_error_deviation),
                        //          minmea_tofloat(&frame.longitude_error_deviation),
                        //          minmea_tofloat(&frame.altitude_error_deviation));
                    }
                    else
                    {
                        ESP_LOGE(TAG, "$xxGST sentence is not parsed\n");
                    }
                }
                break;

                case MINMEA_SENTENCE_GSV:
                {
                    struct minmea_sentence_gsv frame;
                    if (minmea_parse_gsv(&frame, lines[i]))
                    {
                        // ESP_LOGI(TAG, "$xxGSV: message %d of %d", frame.msg_nr, frame.total_msgs);
                        // ESP_LOGI(TAG, "$xxGSV: satellites in view: %d", frame.total_sats);
                        total_sats = frame.total_sats;
                        // for (int i = 0; i < 4; i++)
                        //     printf("$xxGSV: sat nr %d, elevation: %d, azimuth: %d, snr: %d dbm\n",
                        //            frame.sats[i].nr,
                        //            frame.sats[i].elevation,
                        //            frame.sats[i].azimuth,
                        //            frame.sats[i].snr);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "$xxGSV sentence is not parsed\n");
                    }
                }
                break;

                case MINMEA_SENTENCE_VTG:
                {
                    struct minmea_sentence_vtg frame;
                    if (minmea_parse_vtg(&frame, lines[i]))
                    {
                        // ESP_LOGI(TAG, "$xxVTG: true track degrees = %f",
                        //          minmea_tofloat(&frame.true_track_degrees));
                        // ESP_LOGI(TAG, "        magnetic track degrees = %f",
                        //          minmea_tofloat(&frame.magnetic_track_degrees));
                        // ESP_LOGI(TAG, "        speed knots = %f",
                        //          minmea_tofloat(&frame.speed_knots));
                        // ESP_LOGI(TAG, "        speed kph = %f",
                        //          minmea_tofloat(&frame.speed_kph));
                        f_speed_gps = minmea_tofloat(&frame.speed_kph);
                        // speed_gps = minmea_tofloat(&frame.speed_kph);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "$xxVTG sentence is not parsed\n");
                        f_speed_gps = 0;
                    }
                }
                break;

                case MINMEA_SENTENCE_ZDA:
                {
                    struct minmea_sentence_zda frame;
                    if (minmea_parse_zda(&frame, lines[i]))
                    {
                        // ESP_LOGI(TAG, "$xxZDA: %d:%d:%d %02d.%02d.%d UTC%+03d:%02d",
                        //          frame.time.hours,
                        //          frame.time.minutes,
                        //          frame.time.seconds,
                        //          frame.date.day,
                        //          frame.date.month,
                        //          frame.date.year,
                        //          frame.hour_offset,
                        //          frame.minute_offset);
                        if (frame.time.hours != -1 && frame.time.hours != -1 && frame.time.seconds != -1)
                        {
                            hours = frame.time.hours + 8;
                            minutes = frame.time.minutes;
                            seconds = frame.time.seconds;
                            printf("hour minute : %02d:%02d\n", frame.time.hours, frame.time.minutes);
                        }
                        else
                        {
                            hours = 0;
                            minutes = 0;
                        }
                    }
                    else
                    {
                        hours = 0;
                        minutes = 0;
                        ESP_LOGE(TAG, "$xxZDA sentence is not parsed\n");
                    }
                }
                break;

                case MINMEA_INVALID:
                {
                    // ESP_LOGE(TAG, "$xxxxx sentence is not valid\n");
                }
                break;

                default:
                {
                    // ESP_LOGE(TAG, "$xxxxx sentence is not parsed\n");
                }
                break;
                }
                free(lines[i]);
            }
        }
        // 坐标经过卡尔曼滤波
        if (isnan(f_latitude))
        {
            f_latitude_lv = 0;
        }
        else
            f_latitude_lv = kalman_filter_update(&gps_filter1, f_latitude);
        if (isnan(f_longitude))
        {
            f_longitude_lv = 0;
        }
        else
            f_longitude_lv = kalman_filter_update(&gps_filter2, f_longitude);

        if (isnan(f_speed_gps))
        {
            f_speed_gps = 0;
        }
        printf("原坐标系纬度：%lf\n", f_latitude);
        printf("原坐标系经度：%lf\n", f_longitude);
        // 将WGS-84坐标转换为GCJ-02坐标（火星坐标系）
        wgs84_to_gcj02(&f_latitude_lv, &f_longitude_lv);

        // 输出转换后的坐标
        printf("火星坐标系纬度：%lf\n", f_latitude_lv);
        printf("火星坐标系经度：%lf\n", f_longitude_lv);
        // 申请的内存会在发送完成后自动释放
        // dc_data_msg_t *msg = dc_create_msg("gps_topic");
        // msg->msg_type = TEST_MSG_TEST_DATA;
        // gps_data_t *data = calloc(1, sizeof(gps_data_t));
        // data->sats = total_sats;
        // data->speed = speed_gps;
        // data->latitude = latitude;
        // data->longitude = longitude;
        // data->f_latitude = f_latitude;
        // data->f_longitude = f_longitude;
        // data->f_latitude_lv = f_latitude_lv;
        // data->f_longitude_lv = f_longitude_lv;
        // data->f_speed = f_speed_gps;
        // data->hours = hours;
        // data->minutes = minutes;
        // data->seconds = seconds;
        // msg->data = data;
        // // 发布数据
        // dc_publish(msg);

        // // 变化数据
        // a1++;
        // a2 += 2;
        // a3 += 3;
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}

void gps_controller_init()
{
    // 订阅主题

    xTaskCreate((TaskFunction_t)gps_update_task, "gps_update_task",
                4096, NULL, 5,
                NULL);
}