/**
 * Kinesis Video TLS — ESP variant.
 *
 * Drives mbedtls directly through the same BIO-callback shape as upstream's
 * `src/source/Crypto/Tls_mbedtls.c` (so the rest of the KVS SDK doesn't
 * need to know which TLS impl is plugged in). The one ESP-specific
 * difference: certificate verification uses ESP-IDF's compiled-in
 * `esp_crt_bundle` instead of reading a CA cert from the filesystem.
 * That removes the runtime requirement for `KVS_CA_CERT_PATH` on real ESP
 * targets — which usually don't ship a writable filesystem to host a CA
 * bundle in the first place.
 *
 * The interface (createTlsSession, tlsSessionStart, tlsSessionProcessPacket,
 * tlsSessionPutApplicationData, tlsSessionShutdown, freeTlsSession) is the
 * KVS upstream Tls.h contract; the struct it operates on is the
 * mbedtls-backed `__TlsSession` from Tls.h. Functions here mirror the
 * shape of the upstream Tls_mbedtls.c so behavioural drift stays low —
 * if upstream changes the handshake flow or adds error handling, mirror
 * the same change here.
 *
 * Why not patch upstream Tls_mbedtls.c instead? Two reasons: (1) we'd be
 * carrying a forever-patch on a frequently-touched upstream file just for
 * one esp_crt_bundle call. (2) AWS may not accept ESP-platform-specific
 * code into the upstream SDK. Owning a thin parallel implementation is
 * cheaper than maintaining an upstream patch.
 */
#define LOG_CLASS "TLS_esp"
#include "../Include_i.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"

#define TAG "TLS_ESP"

INT32 tlsSessionSendCallback(PVOID customData, const unsigned char* buf, ULONG len)
{
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = (PTlsSession) customData;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);

    pTlsSession->callbacks.outboundPacketFn(pTlsSession->callbacks.outBoundPacketFnCustomData, (PBYTE) buf, len);

CleanUp:

    return STATUS_FAILED(retStatus) ? -retStatus : (INT32) len;
}

INT32 tlsSessionReceiveCallback(PVOID customData, unsigned char* buf, ULONG len)
{
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = (PTlsSession) customData;
    PIOBuffer pBuffer;
    UINT32 readBytes = MBEDTLS_ERR_SSL_WANT_READ;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);

    pBuffer = pTlsSession->pReadBuffer;

    if (pBuffer->off < pBuffer->len) {
        retStatus = ioBufferRead(pBuffer, buf, len, &readBytes);
    }

CleanUp:

    return STATUS_FAILED(retStatus) ? -retStatus : (INT32) readBytes;
}

STATUS createTlsSession(PTlsSessionCallbacks pCallbacks, PTlsSession* ppTlsSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = NULL;

    CHK(ppTlsSession != NULL && pCallbacks != NULL && pCallbacks->outboundPacketFn != NULL, STATUS_NULL_ARG);

    pTlsSession = (PTlsSession) MEMCALLOC(1, SIZEOF(TlsSession));
    CHK(pTlsSession != NULL, STATUS_NOT_ENOUGH_MEMORY);

    CHK_STATUS(createIOBuffer(DEFAULT_MTU_SIZE_BYTES, &pTlsSession->pReadBuffer));
    pTlsSession->callbacks = *pCallbacks;
    pTlsSession->state = TLS_SESSION_STATE_NEW;

    /* mbedtls primitives. cacert is intentionally unused — esp_crt_bundle
     * supplies the trusted-root set in tlsSessionStartWithHostname. We still
     * mbedtls_x509_crt_init() it so freeTlsSession's mbedtls_x509_crt_free()
     * is safe (free on a never-populated chain is a no-op). */
    mbedtls_entropy_init(&pTlsSession->entropy);
    mbedtls_ctr_drbg_init(&pTlsSession->ctrDrbg);
    mbedtls_x509_crt_init(&pTlsSession->cacert);
    mbedtls_ssl_config_init(&pTlsSession->sslCtxConfig);
    mbedtls_ssl_init(&pTlsSession->sslCtx);
    CHK(mbedtls_ctr_drbg_seed(&pTlsSession->ctrDrbg, mbedtls_entropy_func, &pTlsSession->entropy, NULL, 0) == 0,
        STATUS_CREATE_SSL_FAILED);

CleanUp:

    if (STATUS_FAILED(retStatus) && pTlsSession != NULL) {
        freeTlsSession(&pTlsSession);
    }

    if (ppTlsSession != NULL) {
        *ppTlsSession = pTlsSession;
    }

    LEAVES();
    return retStatus;
}

STATUS freeTlsSession(PTlsSession* ppTlsSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PTlsSession pTlsSession = NULL;

    CHK(ppTlsSession != NULL, STATUS_NULL_ARG);

    pTlsSession = *ppTlsSession;
    CHK(pTlsSession != NULL, retStatus);

    mbedtls_entropy_free(&pTlsSession->entropy);
    mbedtls_ctr_drbg_free(&pTlsSession->ctrDrbg);
    mbedtls_x509_crt_free(&pTlsSession->cacert);
    mbedtls_ssl_config_free(&pTlsSession->sslCtxConfig);
    mbedtls_ssl_free(&pTlsSession->sslCtx);

    freeIOBuffer(&pTlsSession->pReadBuffer);
    retStatus = tlsSessionShutdown(pTlsSession);
    SAFE_MEMFREE(*ppTlsSession);

CleanUp:
    return retStatus;
}

STATUS tlsSessionStartWithHostname(PTlsSession pTlsSession, BOOL isServer, PCHAR hostname)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    INT32 sslRet;
    int crtBundleRet;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);
    CHK(pTlsSession->state == TLS_SESSION_STATE_NEW, retStatus);

    CHK(mbedtls_ssl_config_defaults(&pTlsSession->sslCtxConfig, isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0,
        STATUS_CREATE_SSL_FAILED);

    /* ESP-platform difference vs upstream Tls_mbedtls.c: instead of
     * `mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL)` populated from a
     * filesystem read of KVS_CA_CERT_PATH, attach IDF's compiled-in CA
     * bundle. This installs a verification callback that walks the
     * bundle for each chain validation request — no filesystem read,
     * no per-device cert provisioning. */
    crtBundleRet = esp_crt_bundle_attach(&pTlsSession->sslCtxConfig);
    if (crtBundleRet != 0) {
        ESP_LOGE(TAG, "esp_crt_bundle_attach failed: %d", crtBundleRet);
        CHK(FALSE, STATUS_INVALID_CA_CERT_PATH);
    }

    if (hostname != NULL) {
        /* Strict verification when we have a hostname to match. */
        mbedtls_ssl_conf_authmode(&pTlsSession->sslCtxConfig, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        /* Optional verification for IP-based connections (no SNI / no CN to match). */
        mbedtls_ssl_conf_authmode(&pTlsSession->sslCtxConfig, MBEDTLS_SSL_VERIFY_OPTIONAL);
    }

    mbedtls_ssl_conf_rng(&pTlsSession->sslCtxConfig, mbedtls_ctr_drbg_random, &pTlsSession->ctrDrbg);
    CHK(mbedtls_ssl_setup(&pTlsSession->sslCtx, &pTlsSession->sslCtxConfig) == 0, STATUS_SSL_CTX_CREATION_FAILED);

    /* SNI + cert hostname check. Mirrors upstream's mbedtls 3.x guard. */
    if (!isServer && hostname != NULL) {
        CHK(mbedtls_ssl_set_hostname(&pTlsSession->sslCtx, hostname) == 0, STATUS_SSL_CTX_CREATION_FAILED);
    }

    mbedtls_ssl_set_mtu(&pTlsSession->sslCtx, DEFAULT_MTU_SIZE_BYTES);
    mbedtls_ssl_set_bio(&pTlsSession->sslCtx, pTlsSession, tlsSessionSendCallback, tlsSessionReceiveCallback, NULL);

    /* Kick the handshake. The first round-trip's ClientHello is queued via the
     * BIO send callback — actually transmitted by SocketConnection. WANT_READ
     * / WANT_WRITE on first call is normal for non-blocking I/O. */
    tlsSessionChangeState(pTlsSession, TLS_SESSION_STATE_CONNECTING);
    sslRet = mbedtls_ssl_handshake(&pTlsSession->sslCtx);
    CHK(sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE, STATUS_SSL_CTX_CREATION_FAILED);

CleanUp:

    CHK_LOG_ERR(retStatus);

    LEAVES();
    return retStatus;
}

STATUS tlsSessionStart(PTlsSession pTlsSession, BOOL isServer)
{
    return tlsSessionStartWithHostname(pTlsSession, isServer, NULL);
}

STATUS tlsSessionProcessPacket(PTlsSession pTlsSession, PBYTE pData, UINT32 bufferLen, PUINT32 pDataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    INT32 sslRet, readBytes = 0;
    BOOL iterate = TRUE;
    PIOBuffer pReadBuffer;

    CHK(pTlsSession != NULL && pData != NULL && pDataLen != NULL, STATUS_NULL_ARG);
    ESP_LOGW(TAG, "DBG_TLS: processPacket entry tls_state=%d in_bytes=%u sslCtx_state=%d",
             (int)pTlsSession->state, pDataLen ? *pDataLen : 0,
#if MBEDTLS_BEFORE_V3
             (int)pTlsSession->sslCtx.state
#else
             (int)pTlsSession->sslCtx.MBEDTLS_PRIVATE(state)
#endif
            );
    CHK(pTlsSession->state != TLS_SESSION_STATE_NEW, STATUS_SOCKET_CONNECTION_NOT_READY_TO_SEND);
    CHK(pTlsSession->state != TLS_SESSION_STATE_CLOSED, STATUS_SOCKET_CONNECTION_CLOSED_ALREADY);

    pReadBuffer = pTlsSession->pReadBuffer;
    CHK_STATUS(ioBufferWrite(pReadBuffer, pData, *pDataLen));

    /* Consume incoming TLS records — handshake records during CONNECTING,
     * application data records once CONNECTED. mbedtls_ssl_read pulls from
     * pReadBuffer via tlsSessionReceiveCallback. */
    while (iterate && pReadBuffer->off < pReadBuffer->len && bufferLen > 0) {
        sslRet = mbedtls_ssl_read(&pTlsSession->sslCtx, pData + readBytes, bufferLen);
        if (sslRet > 0) {
            readBytes += sslRet;
            bufferLen -= sslRet;
        } else if (sslRet == 0 || sslRet == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            DLOGD("Detected TLS close_notify alert");
            CHK_STATUS(tlsSessionShutdown(pTlsSession));
            iterate = FALSE;
        } else if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE) {
            iterate = FALSE;
        } else {
            ESP_LOGE(TAG, "mbedtls_ssl_read failed: -0x%04x", -sslRet);
            readBytes = 0;
            retStatus = STATUS_INTERNAL_ERROR;
            iterate = FALSE;
        }
    }

#if MBEDTLS_BEFORE_V3
    if (pTlsSession->sslCtx.state == MBEDTLS_SSL_HANDSHAKE_OVER) {
#else
    if (pTlsSession->sslCtx.MBEDTLS_PRIVATE(state) == MBEDTLS_SSL_HANDSHAKE_OVER) {
#endif
        ESP_LOGW(TAG, "DBG_TLS: handshake OVER detected, transitioning to CONNECTED");
        tlsSessionChangeState(pTlsSession, TLS_SESSION_STATE_CONNECTED);
    } else {
        ESP_LOGW(TAG, "DBG_TLS: processPacket exit tls_state=%d sslCtx_state=%d readBytes=%d sslRet_last=%d",
                 (int)pTlsSession->state,
#if MBEDTLS_BEFORE_V3
                 (int)pTlsSession->sslCtx.state,
#else
                 (int)pTlsSession->sslCtx.MBEDTLS_PRIVATE(state),
#endif
                 readBytes, sslRet);
    }

CleanUp:
    if (pDataLen != NULL) {
        *pDataLen = readBytes;
    }

    if (STATUS_FAILED(retStatus)) {
        DLOGD("Warning: reading socket data failed with 0x%08x", retStatus);
    }

    LEAVES();
    return retStatus;
}

STATUS tlsSessionPutApplicationData(PTlsSession pTlsSession, PBYTE pData, UINT32 dataLen)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 writtenBytes = 0;
    BOOL iterate = TRUE;
    INT32 sslRet;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);

    while (iterate && writtenBytes < dataLen) {
        sslRet = mbedtls_ssl_write(&pTlsSession->sslCtx, pData + writtenBytes, dataLen - writtenBytes);
        if (sslRet > 0) {
            writtenBytes += sslRet;
        } else if (sslRet == MBEDTLS_ERR_SSL_WANT_READ || sslRet == MBEDTLS_ERR_SSL_WANT_WRITE) {
            iterate = FALSE;
        } else {
            ESP_LOGE(TAG, "mbedtls_ssl_write failed: -0x%04x", -sslRet);
            writtenBytes = 0;
            retStatus = STATUS_INTERNAL_ERROR;
            iterate = FALSE;
        }
    }

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS tlsSessionShutdown(PTlsSession pTlsSession)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);
    CHK(pTlsSession->state != TLS_SESSION_STATE_CLOSED, retStatus);

    while (mbedtls_ssl_close_notify(&pTlsSession->sslCtx) == MBEDTLS_ERR_SSL_WANT_WRITE) {
        /* keep flushing outgoing buffer until nothing left */
    }
    CHK_STATUS(tlsSessionChangeState(pTlsSession, TLS_SESSION_STATE_CLOSED));

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}

STATUS tlsSessionChangeState(PTlsSession pTlsSession, TLS_SESSION_STATE newState)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pTlsSession != NULL, STATUS_NULL_ARG);
    CHK(pTlsSession->state != newState, retStatus);

    ESP_LOGW(TAG, "DBG_TLS: state %d -> %d", (int)pTlsSession->state, (int)newState);
    pTlsSession->state = newState;

    if (pTlsSession->callbacks.stateChangeFn != NULL) {
        pTlsSession->callbacks.stateChangeFn(pTlsSession->callbacks.stateChangeFnCustomData, newState);
    }

CleanUp:
    return retStatus;
}
