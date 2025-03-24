// Handy debug serial logging

#ifdef _DEBUG_
#define _PP(...) Serial.print(__VA_ARGS__);
#define _PL(...) Serial.println(__VA_ARGS__);
#define _PF(...) Serial.printf(__VA_ARGS__);
#else
#define _PP(a);
#define _PL(a);
#define _PF(...);
#endif