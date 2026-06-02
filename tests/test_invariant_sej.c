#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* External function from sej.c */
extern int sej_aes_crypt(uint8_t *plaintext, uint32_t plaintext_len,
                         uint8_t *ciphertext, uint32_t *ciphertext_len,
                         uint8_t *key, uint32_t key_len, uint8_t *iv);

START_TEST(test_sej_key_isolation_under_adversarial_input)
{
    /* Invariant: SEJ encryption must not use predictable hardcoded keys
       that can be extracted from firmware. Each encryption operation must
       produce different ciphertexts for identical plaintexts when using
       different keys/IVs, proving keys are not fixed defaults. */

    uint8_t plaintext[16] = "AAAAAAAAAAAAAAAA";
    uint8_t ciphertext1[32] = {0};
    uint8_t ciphertext2[32] = {0};
    uint32_t ct_len1 = sizeof(ciphertext1);
    uint32_t ct_len2 = sizeof(ciphertext2);

    /* Adversarial payloads: attempt to trigger default key usage */
    uint8_t default_like_key[32] = {0};  /* All zeros - common default */
    uint8_t default_like_iv[16] = {0};   /* All zeros - common default */
    uint8_t random_key[32];
    uint8_t random_iv[16];

    /* Fill random key/IV with non-zero values */
    for (int i = 0; i < 32; i++) random_key[i] = (i + 1) * 7;
    for (int i = 0; i < 16; i++) random_iv[i] = (i + 2) * 13;

    /* Test 1: Encrypt with default-like key */
    int ret1 = sej_aes_crypt(plaintext, sizeof(plaintext),
                             ciphertext1, &ct_len1,
                             default_like_key, sizeof(default_like_key),
                             default_like_iv);

    /* Test 2: Encrypt same plaintext with different key */
    int ret2 = sej_aes_crypt(plaintext, sizeof(plaintext),
                             ciphertext2, &ct_len2,
                             random_key, sizeof(random_key),
                             random_iv);

    /* Invariant checks */
    ck_assert_int_eq(ret1, 0);  /* Encryption must succeed */
    ck_assert_int_eq(ret2, 0);  /* Encryption must succeed */
    ck_assert_int_eq(ct_len1, ct_len2);  /* Same plaintext length */

    /* CRITICAL: Ciphertexts must differ when keys differ.
       If they are identical, the engine is using hardcoded keys
       regardless of input, violating the security boundary. */
    int ciphertexts_differ = memcmp(ciphertext1, ciphertext2, ct_len1) != 0;
    ck_assert_msg(ciphertexts_differ,
                  "SEJ engine produced identical ciphertexts with different keys: "
                  "hardcoded key vulnerability detected");
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("SEJ_Security");
    tc_core = tcase_create("KeyIsolation");

    tcase_add_test(tc_core, test_sej_key_isolation_under_adversarial_input);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}