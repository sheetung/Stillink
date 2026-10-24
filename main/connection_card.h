#pragma once
#include <stdbool.h>
void connection_card_init(void);
void connection_card_request(void);
void connection_card_reset_notice(void);
bool connection_card_holding(void);
void connection_card_release(void);

int connection_card_render(bool reset);
