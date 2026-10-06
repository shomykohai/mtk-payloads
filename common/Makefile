srcs-y += common/debug.c
srcs-y += common/libc.c

srcs-$(CONFIG_UART) += common/drivers/uart/uart.c

srcs-$(CONFIG_CRYPTO) += common/crypto/xor.c
srcs-$(CONFIG_CRYPTO) += common/crypto/hmac-sha256.c
srcs-$(CONFIG_CRYPTO) += common/crypto/sha256.c

srcs-$(CONFIG_CRYPTO_TZCC) += common/crypto/tzcc.c
srcs-$(CONFIG_CRYPTO_TZCC) += common/crypto/sbrom/sbrom.c

ifeq ($(CONFIG_CRYPTO_KDF),y)
$(call needs,CRYPTO_KDF,CRYPTO_TZCC)
endif
srcs-$(CONFIG_CRYPTO_KDF) += common/crypto/key_derive.c

ifeq ($(CONFIG_CRYPTO_SEJ),y)
$(call needs,CRYPTO_SEJ,CRYPTO)
endif
srcs-$(CONFIG_CRYPTO_SEJ) += common/crypto/sej/sej.c
srcs-$(CONFIG_CRYPTO_SEJ) += common/crypto/sej/sej_hk.c
srcs-$(CONFIG_CRYPTO_SEJ) += common/crypto/sej/sej_sk.c
srcs-$(CONFIG_CRYPTO_SEJ) += common/drivers/sgpt/sgpt.c

$(call arch_only,CRYPTO_SSR,arm64)
srcs-$(CONFIG_CRYPTO_SSR) += common/crypto/ssr/top.c
srcs-$(CONFIG_CRYPTO_SSR) += common/crypto/ssr/kdf.c
srcs-$(CONFIG_CRYPTO_SSR) += common/crypto/ssr/pka.c
srcs-$(CONFIG_CRYPTO_SSR) += common/crypto/ssr/ccc.c
srcs-$(CONFIG_CRYPTO_SSR) += common/crypto/ssr/ssr.c

srcs-$(CONFIG_MMC) += common/storage/mmc/mmc.c

srcs-$(CONFIG_RPMB) += common/security/rpmb.c

ifeq ($(CONFIG_MMC_RPMB),y)
$(call needs,MMC_RPMB,MMC)
$(call needs,MMC_RPMB,RPMB)
endif
srcs-$(CONFIG_MMC_RPMB) += common/storage/mmc/rpmb_mmc.c

srcs-$(CONFIG_XML) += common/xml.c
srcs-$(CONFIG_XML) += common/yxml.c

cflags-common/crypto/hmac-sha256.c    += -O3
cflags-common/crypto/sha256.c         += -O3
cflags-common/security/rpmb.c         += -O3
cflags-common/storage/mmc/mmc.c       += -O3
cflags-common/storage/mmc/rpmb_mmc.c  += -O3
cflags-common/xml.c                   += -O3
cflags-common/yxml.c                  += -O3
