#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

void unity_output_char(int c);

#define UNITY_OUTPUT_CHAR(a) unity_output_char((int)(a))
#define UNITY_OUTPUT_FLUSH() ((void)0)
#define UNITY_OUTPUT_START() ((void)0)
#define UNITY_OUTPUT_COMPLETE() ((void)0)

#ifdef __cplusplus
}
#endif

#endif
