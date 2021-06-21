#pragma once

#ifndef numberof
#define numberof(x)	(sizeof((x)) / sizeof((x)[0]))
#endif
#ifndef endof
#define endof(x)	((x) + numberof((x)))
#endif
#define bitflag(x)	((1<<(x)))
