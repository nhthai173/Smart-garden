#pragma once

#include <Arduino.h>

/* base64_to_binary:
 *   Description:
 *     Converts a single byte from a base64 character to the corresponding binary value
 *   Parameters:
 *     c - Base64 character (as ascii code)
 *   Returns:
 *     6-bit binary value
 */
unsigned char base64_to_binary(unsigned char c);



/* decode_base64_length:
 *   Description:
 *     Calculates number of bytes of binary data in a base64 string
 *     Variant that does not use input_length no longer used within library, retained for API compatibility
 *   Parameters:
 *     input - Base64-encoded null-terminated string
 *     input_length (optional) - Number of bytes to read from input pointer
 *   Returns:
 *     Number of bytes of binary data in input
 */
unsigned int decode_base64_length(const unsigned char input[]);
unsigned int decode_base64_length(const unsigned char input[], unsigned int input_length);
size_t decode_base64_length(const String& input);



/* decode_base64:
 *   Description:
 *     Converts a base64 null-terminated string to an array of bytes
 *   Parameters:
 *     input - Pointer to input string
 *     input_length (optional) - Number of bytes to read from input pointer
 *     output - Pointer to output array
 *   Returns:
 *     Number of bytes in the decoded binary
 */
unsigned int decode_base64(const unsigned char input[], unsigned char output[]);
unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]);
String decode_base64(const String& input);



unsigned char base64_to_binary(unsigned char c) {
    // Capital letters - 'A' is ascii 65 and base64 0
    if('A' <= c && c <= 'Z') return c - 'A';

    // Lowercase letters - 'a' is ascii 97 and base64 26
    if('a' <= c && c <= 'z') return c - 71;

    // Digits - '0' is ascii 48 and base64 52
    if('0' <= c && c <= '9') return c + 4;

#ifdef BASE64_URL
    // '-' is ascii 45 and base64 62
  if(c == '-') return 62;
#else
    // '+' is ascii 43 and base64 62
    if(c == '+') return 62;
#endif

#ifdef BASE64_URL
    // '_' is ascii 95 and base64 62
  if(c == '_') return 63;
#else
    // '/' is ascii 47 and base64 63
    if(c == '/') return 63;
#endif

    return 255;
}

unsigned int decode_base64_length(const unsigned char input[]) {
    return decode_base64_length(input, -1);
}

unsigned int decode_base64_length(const unsigned char input[], unsigned int input_length) {
    const unsigned char *start = input;

    while(base64_to_binary(input[0]) < 64 && (unsigned int) (input - start) < input_length) {
        ++input;
    }

    input_length = (unsigned int) (input - start);
    return input_length/4*3 + (input_length % 4 ? input_length % 4 - 1 : 0);
}

size_t decode_base64_length(const String& input) {
    return decode_base64_length((const unsigned char*) input.c_str(), input.length());
}

unsigned int decode_base64(const unsigned char input[], unsigned char output[]) {
    return decode_base64(input, -1, output);
}

unsigned int decode_base64(const unsigned char input[], unsigned int input_length, unsigned char output[]) {
    unsigned int output_length = decode_base64_length(input, input_length);

    // While there are still full sets of 24 bits...
    for(unsigned int i = 2; i < output_length; i += 3) {
        output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
        output[1] = base64_to_binary(input[1]) << 4 | base64_to_binary(input[2]) >> 2;
        output[2] = base64_to_binary(input[2]) << 6 | base64_to_binary(input[3]);

        input += 4;
        output += 3;
    }

    switch(output_length % 3) {
        case 1:
            output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
            break;
        case 2:
            output[0] = base64_to_binary(input[0]) << 2 | base64_to_binary(input[1]) >> 4;
            output[1] = base64_to_binary(input[1]) << 4 | base64_to_binary(input[2]) >> 2;
            break;
    }

    return output_length;
}

String decode_base64(const String& input) {
    String output;
    output.reserve(decode_base64_length(input));

    unsigned char buffer[3];
    unsigned int output_length = decode_base64((const unsigned char*) input.c_str(), input.length(), buffer);

    for(unsigned int i = 0; i < output_length; ++i) {
        output += (char) buffer[i];
    }

    return output;
}