#pragma once

#include <stdint.h>

/**
 * @brief Convert 16-bit value from network byte order to host byte order.
 *
 * Network byte order is big-endian as defined by POSIX/BSD sockets APIs.
 *
 * @param netshort 16-bit value in network byte order.
 * @return 16-bit value in the host's native byte order.
 */
uint16_t ntohs(uint16_t netshort);

/**
 * @brief Convert 16-bit value from host byte order to network byte order.
 *
 * Network byte order is big-endian as defined by POSIX/BSD sockets APIs.
 *
 * @param hostshort 16-bit value in host byte order.
 * @return 16-bit value converted to network byte order.
 */
uint16_t htons(uint16_t hostshort);

/**
 * @brief Convert 32-bit value from network byte order to host byte order.
 *
 * Network byte order is big-endian as defined by POSIX/BSD sockets APIs.
 *
 * @param netlong 32-bit value in network byte order.
 * @return 32-bit value in the host's native byte order.
 */
uint32_t ntohl(uint32_t netlong);

/**
 * @brief Convert 32-bit value from host byte order to network byte order.
 *
 * Network byte order is big-endian as defined by POSIX/BSD sockets APIs.
 *
 * @param hostlong 32-bit value in host byte order.
 * @return 32-bit value converted to network byte order.
 */
uint32_t htonl(uint32_t hostlong);

/**
 * @brief Convert an IPv4 dotted-decimal string to a 32-bit address in
 *        network byte order.
 *
 * This function parses strings like "A.B.C.D" where each octet is in the
 * range 0..255. On invalid input, returns 0, which is also the value of
 * INADDR_ANY. Callers that need to distinguish invalid input from the
 * valid address 0.0.0.0 should validate the string format separately.
 *
 * @param cp Pointer to a NUL-terminated IPv4 dotted-decimal string.
 * @return 32-bit IPv4 address in network byte order, or 0 on invalid input.
 */
uint32_t inet_addr(const char *cp);

/**
 * @brief Convert a 32-bit IPv4 address in network byte order to a
 *        dotted-decimal string.
 *
 * The result is written to the provided buffer as a NUL-terminated string.
 * The buffer must be large enough to hold the longest possible IPv4 string
 * "255.255.255.255" and the terminator, i.e., at least 16 bytes.
 *
 * @param addr 32-bit IPv4 address in network byte order.
 * @param buf Output buffer with capacity of at least 16 bytes.
 */
void inet_ntoa_r(uint32_t addr, char *buf);

/**
 * Convert a binary representation of an IPv4 address to its byte array form.
 * @param ip The binary representation of the IPv4 address.
 * @param out Buffer to store the byte array representation of the IPv4 address.
 */
void ip_to_bytes(uint32_t ip, uint8_t out[4]);

/**
 * Convert a byte array IPv4 address to its binary representation.
 * @param bytes The byte array representation of the IPv4 address.
 * @param out Output pointer for the binary representation.
 */
void bytes_to_ip(const uint8_t bytes[4], uint32_t* out);
