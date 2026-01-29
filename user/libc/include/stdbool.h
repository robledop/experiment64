#pragma once

#if defined __STDC_VERSION__ && __STDC_VERSION__ < 202000 // C23
#define bool _Bool
#define true 1
#define false 0
#endif