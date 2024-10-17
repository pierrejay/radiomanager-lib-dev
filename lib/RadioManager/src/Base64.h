#ifndef BASE64_H
#define BASE64_H

#include <Arduino.h>
#include <vector>

using Bytes = std::vector<uint8_t>;

class Base64 {
public:
    static String encode(const uint8_t* data, size_t length) {
        static const char* encoding_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t output_length = 4 * ((length + 2) / 3);
        String encoded;
        encoded.reserve(output_length);

        for (size_t i = 0; i < length;) {
            uint32_t octet_a = i < length ? data[i++] : 0;
            uint32_t octet_b = i < length ? data[i++] : 0;
            uint32_t octet_c = i < length ? data[i++] : 0;

            uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

            encoded += encoding_table[(triple >> 3 * 6) & 0x3F];
            encoded += encoding_table[(triple >> 2 * 6) & 0x3F];
            encoded += encoding_table[(triple >> 1 * 6) & 0x3F];
            encoded += encoding_table[(triple >> 0 * 6) & 0x3F];
        }

        switch (length % 3) {
            case 1:
                encoded[output_length - 1] = '=';
                encoded[output_length - 2] = '=';
                break;
            case 2:
                encoded[output_length - 1] = '=';
                break;
        }

        return encoded;
    }

    static String encode(const Bytes& data) {
        return encode(data.data(), data.size());
    }

    static size_t decode(const String& input, uint8_t* output) {
        static const uint8_t decoding_table[256] = {
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
            64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
            15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
            64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
            41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
            64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
        };

        size_t input_length = input.length();
        if (input_length % 4 != 0) return 0;

        size_t output_length = input_length / 4 * 3;
        if (input[input_length - 1] == '=') output_length--;
        if (input[input_length - 2] == '=') output_length--;

        size_t i, j;
        for (i = 0, j = 0; i < input_length;) {
            uint32_t sextet_a = input[i] == '=' ? 0 & i++ : decoding_table[input[i++]];
            uint32_t sextet_b = input[i] == '=' ? 0 & i++ : decoding_table[input[i++]];
            uint32_t sextet_c = input[i] == '=' ? 0 & i++ : decoding_table[input[i++]];
            uint32_t sextet_d = input[i] == '=' ? 0 & i++ : decoding_table[input[i++]];

            uint32_t triple = (sextet_a << 3 * 6)
                + (sextet_b << 2 * 6)
                + (sextet_c << 1 * 6)
                + (sextet_d << 0 * 6);

            if (j < output_length) output[j++] = (triple >> 2 * 8) & 0xFF;
            if (j < output_length) output[j++] = (triple >> 1 * 8) & 0xFF;
            if (j < output_length) output[j++] = (triple >> 0 * 8) & 0xFF;
        }

        return output_length;
    }

    static Bytes decode(const String& input) {
        Bytes output;
        decode(input, output);
        return output;
    }

    static bool decode(const String& input, Bytes& output) {
        size_t output_length = decodedLength(input.c_str(), input.length());
        output.resize(output_length);
        size_t decoded_size = decode(input, output.data());
        return decoded_size == output_length;
    }

    static size_t encodedLength(size_t length) {
        return 4 * ((length + 2) / 3);
    }

    static size_t decodedLength(const char* input, size_t input_length) {
        size_t padding = 0;
        if (input_length > 0 && input[input_length - 1] == '=') padding++;
        if (input_length > 1 && input[input_length - 2] == '=') padding++;
        return (input_length / 4) * 3 - padding;
    }
};

#endif // BASE64_H