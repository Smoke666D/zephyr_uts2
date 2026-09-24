/*
 * luaeheap.h
 *
 *  Created on: 21 апр. 2022 г.
 *      Author: igor.dymov
 */

#ifndef LUAEHEAP_H_
#define LUAEHEAP_H_



#ifndef DONT_USE_LUA_HEAP_MANAGEMENT_FUNCTIONS
void *luaMalloc( size_t xWantedSize );
void luaFree( void *pv );
size_t LuaGetFreeHeapSize( void );
size_t LuaGetMinimumEverFreeHeapSize(void);
#endif



#endif /* LUAEHEAP_H_ */
