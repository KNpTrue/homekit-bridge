/**
 * Copyright (c) 2021 KNpTrue
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/
#ifndef LHAPLIB_H
#define LHAPLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lua.h>
#include <HAP.h>

#define LUA_HAPNAME "hap"
LUAMOD_API int luaopen_hap(lua_State *L);

/**
 * Get accessory.
 */
const HAPAccessory *lhap_get_accessory(void);

/**
 * Get bridged accessories.
 */
const HAPAccessory *const *lhap_get_bridged_accessories(void);

/**
 * Get attribute count.
 */
size_t lhap_get_attribute_count(void);

/**
 * De-initialize lhap.
 */
void lhap_deinitialize();

#ifdef __cplusplus
}
#endif

#endif /* LHAPLIB_H */
