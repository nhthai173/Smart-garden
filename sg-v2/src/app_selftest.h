#ifndef APP_SELFTEST_H
#define APP_SELFTEST_H
/** Chỉ có khi build với -DSG_SIM. Chạy kiểm thử tích hợp trên chip. */
void app_selftest_start(void);
#endif
