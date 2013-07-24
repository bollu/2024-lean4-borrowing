/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
namespace lean {

void mix(unsigned & a, unsigned & b, unsigned & c) {
    a -= b; a -= c; a ^= (c>>13);
    b -= c; b -= a; b ^= (a<<8);
    c -= a; c -= b; c ^= (b>>13);
    a -= b; a -= c; a ^= (c>>12);
    b -= c; b -= a; b ^= (a<<16);
    c -= a; c -= b; c ^= (b>>5);
    a -= b; a -= c; a ^= (c>>3);
    b -= c; b -= a; b ^= (a<<10);
    c -= a; c -= b; c ^= (b>>15);
}

// Bob Jenkin's hash function.
// http://burtleburtle.net/bob/hash/doobs.html
unsigned hash_str(unsigned length, char const * str, unsigned init_value) {
    register unsigned a, b, c, len;

    /* Set up the internal state */
    len = length;
    a = b = 0x9e3779b9;  /* the golden ratio; an arbitrary value */
    c = init_value;      /* the previous hash value */

    /*---------------------------------------- handle most of the key */
    while (len >= 12) {
        a += reinterpret_cast<unsigned const *>(str)[0];
        b += reinterpret_cast<unsigned const *>(str)[1];
        c += reinterpret_cast<unsigned const *>(str)[2];
        mix(a,b,c);
        str += 12; len -= 12;
    }

    /*------------------------------------- handle the last 11 bytes */
    c += length;
    switch(len) {
        /* all the case statements fall through */
    case 11:  c+=((unsigned)str[10]<<24);
    case 10:  c+=((unsigned)str[9]<<16);
    case 9 :  c+=((unsigned)str[8]<<8);
        /* the first byte of c is reserved for the length */
    case 8 :  b+=((unsigned)str[7]<<24);
    case 7 :  b+=((unsigned)str[6]<<16);
    case 6 :  b+=((unsigned)str[5]<<8);
    case 5 :  b+=str[4];
    case 4 :  a+=((unsigned)str[3]<<24);
    case 3 :  a+=((unsigned)str[2]<<16);
    case 2 :  a+=((unsigned)str[1]<<8);
    case 1 :  a+=str[0];
        /* case 0: nothing left to add */
    }
    mix(a,b,c);
    /*-------------------------------------------- report the result */
    return c;
}

}
