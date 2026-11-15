/* Display-only locale adapter; menu keys and data stay English/ASCII. */
#ifndef DPC_I18N_H
#define DPC_I18N_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void DPC_I18n_SetChinese(uint8_t enable);
uint8_t DPC_I18n_IsChinese(void);
const char *DPC_I18n_Text(const char *english);
const uint8_t *DPC_I18n_Font(void);
/* Already translated UTF-8 text, drawn/measured with the UI locale font. */
uint16_t DPC_I18n_TextWidth(const char *text);
uint16_t DPC_I18n_DrawText(uint16_t x, uint16_t y, const char *text);
/* Translate an original English key and draw it. */
uint16_t DPC_I18n_DrawLabel(uint16_t x, uint16_t y, const char *english);
/* Byte length of the next complete code point in a NUL-terminated string. */
unsigned DPC_I18n_CharBytes(const char *text);
#ifdef __cplusplus
}
#endif
#endif
