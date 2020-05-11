#ifndef __UTILS_H__
#define __UTILS_H__

#define max(a,b) ({ __typeof__ (a) _max_a = (a); __typeof__ (b) _max_b = (b); 	_max_a > _max_b ? _max_a : _max_b; })
#define min(a,b) ({ __typeof__ (a) _min_a = (a); __typeof__ (b) _min_b = (b); 	_min_a < _min_b ? _min_a : _min_b; })

#endif