//  encrypt.c

#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cose/cose.h>
#include <cose/cose_configure.h>
#include <cn-cbor/cn-cbor.h>
#if (INCLUDE_ENCRYPT || INCLUDE_ENCRYPT0 || INCLUDE_MAC) && \
	(!INCLUDE_MAC || !INCLUDE_SIGN)
#include <cose_int.h>
#endif
#include "json.h"
#include "test.h"
#include "context.h"
#include "cose_int.h"

#ifdef _MSC_VER
#pragma warning(disable : 4127)
#endif

#if INCLUDE_ENCRYPT
bool DecryptMessage(const byte *pbEncoded,
	size_t cbEncoded,
	bool fFailBody,
	const cn_cbor *pEnveloped,
	const cn_cbor *pRecipient1,
	int iRecipient1,
	const cn_cbor *pRecipient2,
	int iRecipient2)
{
	HCOSE_ENVELOPED hEnc = NULL;
	HCOSE_RECIPIENT hRecip = NULL;
	HCOSE_RECIPIENT hRecip1 = NULL;
	HCOSE_RECIPIENT hRecip2 = NULL;
	bool fRet = false;
	int type = 0;
	cose_errback cose_err;
	cn_cbor *pkey = NULL;
	bool fNoSupport = false;

	if (false) {
	errorReturn:
		if (hEnc != NULL) {
			COSE_Enveloped_Free(hEnc);
		}
		if (hRecip1 != NULL) {
			COSE_Recipient_Free(hRecip1);
		}
		if (hRecip2 != NULL) {
			COSE_Recipient_Free(hRecip2);
		}

		return fRet;		
	}
	
	hEnc = (HCOSE_ENVELOPED)COSE_Decode(pbEncoded, cbEncoded, &type,
		COSE_enveloped_object, CBOR_CONTEXT_PARAM_COMMA & cose_err);
	if (hEnc == NULL) {
		if (fFailBody && (cose_err.err == COSE_ERR_INVALID_PARAMETER)) {
			return true;
		}
		goto errorReturn;
	}

	if (!SetReceivingAttributes(
			(HCOSE)hEnc, pEnveloped, Attributes_Enveloped_protected)) {
		goto errorReturn;
	}

	cn_cbor *alg = COSE_Enveloped_map_get_int(
		hEnc, COSE_Header_Algorithm, COSE_BOTH, NULL);
	if (!IsAlgorithmSupported(alg)) {
		fNoSupport = true;
	}

	hRecip1 = COSE_Enveloped_GetRecipient(hEnc, iRecipient1, NULL);
	if (hRecip1 == NULL) {
		goto errorReturn;
	}
	if (!SetReceivingAttributes(
			(HCOSE)hRecip1, pRecipient1, Attributes_Recipient_protected)) {
		goto errorReturn;
	}

	if (pRecipient2 != NULL) {
		pkey = BuildKey(cn_cbor_mapget_string(pRecipient2, "key"), false);
		if (pkey == NULL) {
			goto errorReturn;
		}

		hRecip2 = COSE_Recipient_GetRecipient(hRecip1, iRecipient2, NULL);
		if (hRecip2 == NULL) {
			goto errorReturn;
		}

		if (!SetReceivingAttributes(
				(HCOSE)hRecip2, pRecipient2, Attributes_Recipient_protected)) {
			goto errorReturn;
		}
		if (!COSE_Recipient_SetKey(hRecip2, pkey, NULL)) {
			goto errorReturn;
		}

		cn_cbor *cnStatic = cn_cbor_mapget_string(pRecipient2, "sender_key");
		if (cnStatic != NULL) {
			if (COSE_Recipient_map_get_int(
					hRecip2, COSE_Header_ECDH_SPK, COSE_BOTH, NULL) == 0) {
				COSE_Recipient_map_put_int(hRecip2, COSE_Header_ECDH_SPK,
					BuildKey(cnStatic, true), COSE_DONT_SEND, NULL);
			}
		}

		hRecip = hRecip2;
	}
	else {
		pkey = BuildKey(cn_cbor_mapget_string(pRecipient1, "key"), false);
		if (pkey == NULL) {
			goto errorReturn;
		}
		if (!COSE_Recipient_SetKey(hRecip1, pkey, NULL)) {
			goto errorReturn;
		}

		cn_cbor *cnStatic = cn_cbor_mapget_string(pRecipient1, "sender_key");
		if (cnStatic != NULL) {
			if (COSE_Recipient_map_get_int(
					hRecip1, COSE_Header_ECDH_SPK, COSE_BOTH, NULL) == 0) {
				COSE_Recipient_map_put_int(hRecip1, COSE_Header_ECDH_SPK,
					BuildKey(cnStatic, true), COSE_DONT_SEND, NULL);
			}
		}

		hRecip = hRecip1;
	}

	if (!fFailBody) {
		cn_cbor *cn = cn_cbor_mapget_string(pRecipient1, "fail");
		if (cn != NULL && (cn->type == CN_CBOR_TRUE)) {
			fFailBody = true;
		}
		if (fFailBody && (pRecipient2 != NULL)) {
			cn = cn_cbor_mapget_string(pRecipient2, "fail");
			if (cn != NULL && (cn->type == CN_CBOR_TRUE)) {
				fFailBody = true;
			}
		}

		if (hRecip2 != NULL) {
			alg = COSE_Recipient_map_get_int(
				hRecip2, COSE_Header_Algorithm, COSE_BOTH, NULL);
			if (!IsAlgorithmSupported(alg)) {
				fNoSupport = true;
			}
		}
		alg = COSE_Recipient_map_get_int(
			hRecip1, COSE_Header_Algorithm, COSE_BOTH, NULL);
		if (!IsAlgorithmSupported(alg)) {
			fNoSupport = true;
		}
	}

	if (COSE_Enveloped_decrypt(hEnc, hRecip, &cose_err)) {
		fRet = !fFailBody;
	}
	else {
		if (cose_err.err == COSE_ERR_NO_COMPRESSED_POINTS ||
			cose_err.err == COSE_ERR_UNKNOWN_ALGORITHM) {
			fRet = false;
			fNoSupport = true;
		}
		else if (fNoSupport) {
			fRet = false;
		}
		else {
			fRet = fFailBody;
		}
	}

#if INCLUDE_COUNTERSIGNATURE
	//  Countersign on Encrypt0 Body

	//  Validate counter signatures on signers
	cn_cbor *countersignList =
		cn_cbor_mapget_string(pRecipient1, "countersign");
	if (countersignList != NULL) {
		cn_cbor *countersigners =
			cn_cbor_mapget_string(countersignList, "signers");
		if (countersigners == NULL) {
			fRet = false;
			goto errorReturn;
		}
		int count = countersigners->length;
		bool forward = true;

		if (COSE_Recipient_map_get_int(hRecip1, COSE_Header_CounterSign,
				COSE_UNPROTECT_ONLY, 0) == NULL) {
			goto errorReturn;
		}

		for (int counterNo = 0; counterNo < count; counterNo++) {
			bool noSupportSign = false;

			HCOSE_COUNTERSIGN h =
				COSE_Recipient_get_countersignature(hRecip1, counterNo, 0);
			if (h == NULL) {
				fRet = false;
				continue;
			}

			cn_cbor *counterSigner = cn_cbor_index(
				countersigners, forward ? counterNo : count - counterNo - 1);

			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(counterSigner, "key"), false);
			if (pkeyCountersign == NULL) {
				fRet = false;
				COSE_CounterSign_Free(h);
				continue;
			}

			if (!COSE_CounterSign_SetKey(h, pkeyCountersign, 0)) {
				fRet = false;
				COSE_CounterSign_Free(h);
				CN_CBOR_FREE(pkeyCountersign, context);
				continue;
			}

			alg = COSE_CounterSign_map_get_int(
				h, COSE_Header_Algorithm, COSE_BOTH, NULL);
			if (!IsAlgorithmSupported(alg)) {
				noSupportSign = true;
				fNoSupport = true;
			}

			if (COSE_Recipient_CounterSign_validate(hRecip1, h, 0)) {
				//  I don't think we have any forced errors yet.
			}
			else {
				if (forward && counterNo == 0 && count > 1) {
					forward = false;
					counterNo -= 1;
				}
				else {
					fRet = !noSupportSign;
				}
			}

			COSE_CounterSign_Free(h);
		}
	}
#endif

#if INCLUDE_COUNTERSIGNATURE
	//  Countersign on Enveloped Body

	//  Validate counter signatures on signers
	countersignList = cn_cbor_mapget_string(pEnveloped, "countersign");
	if (countersignList != NULL) {
		cn_cbor *countersigners =
			cn_cbor_mapget_string(countersignList, "signers");
		if (countersigners == NULL) {
			fRet = false;
			goto errorReturn;
		}
		int count = countersigners->length;
		bool forward = true;

		if (COSE_Enveloped_map_get_int(hEnc, COSE_Header_CounterSign,
				COSE_UNPROTECT_ONLY, 0) == NULL) {
			fRet = false;
			goto errorReturn;
		}

		for (int counterNo = 0; counterNo < count; counterNo++) {
			bool noSupportSign = false;
			HCOSE_COUNTERSIGN h =
				COSE_Enveloped_get_countersignature(hEnc, counterNo, 0);
			if (h == NULL) {
				fRet = false;
				continue;
			}

			cn_cbor *counterSigner = cn_cbor_index(
				countersigners, forward ? counterNo : count - counterNo - 1);

			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(counterSigner, "key"), false);
			if (pkeyCountersign == NULL) {
				fRet = false;
				COSE_CounterSign_Free(h);
				continue;
			}

			if (!COSE_CounterSign_SetKey(h, pkeyCountersign, 0)) {
				fRet = false;
				COSE_CounterSign_Free(h);
				CN_CBOR_FREE(pkeyCountersign, context);
				continue;
			}

			alg = COSE_CounterSign_map_get_int(
				h, COSE_Header_Algorithm, COSE_BOTH, NULL);
			if (!IsAlgorithmSupported(alg)) {
				noSupportSign = true;
				fNoSupport = true;
			}

			if (COSE_Enveloped_CounterSign_validate(hEnc, h, 0)) {
				//  I don't think we have any forced errors yet.
			}
			else {
				if (forward && counterNo == 0 && count > 1) {
					forward = false;
					counterNo -= 1;
				}
				else {
					fRet = !noSupportSign;
				}
			}

			COSE_CounterSign_Free(h);
		}
	}
#endif

	if (!fRet && !fNoSupport) {
		CFails++;
	}
	goto errorReturn;
}

int _ValidateEnveloped(const cn_cbor *pControl,
	const byte *pbEncoded,
	size_t cbEncoded)
{
	const cn_cbor *pInput = cn_cbor_mapget_string(pControl, "input");
	const cn_cbor *pFail;
	const cn_cbor *pEnveloped;
	const cn_cbor *pRecipients;
	int iRecipient;
	bool fFailBody = false;
	int passCount = 0;

	pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		fFailBody = true;
	}

	if ((pInput == NULL) || (pInput->type != CN_CBOR_MAP)) {
		goto errorReturn;
	}
	pEnveloped = cn_cbor_mapget_string(pInput, "enveloped");
	if ((pEnveloped == NULL) || (pEnveloped->type != CN_CBOR_MAP)) {
		goto errorReturn;
	}

	pRecipients = cn_cbor_mapget_string(pEnveloped, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) {
		goto errorReturn;
	}

	iRecipient = (int)pRecipients->length - 1;
	pRecipients = pRecipients->first_child;
	for (; pRecipients != NULL; iRecipient--, pRecipients = pRecipients->next) {
		cn_cbor *pRecip2 = cn_cbor_mapget_string(pRecipients, "recipients");
		if (pRecip2 == NULL) {
			if (DecryptMessage(pbEncoded, cbEncoded, fFailBody, pEnveloped,
					pRecipients, iRecipient, NULL, 0)) {
				passCount++;
			}
		}
		else {
			int iRecipient2 = (int)(pRecip2->length - 1);
			pRecip2 = pRecip2->first_child;
			for (; pRecip2 != NULL; pRecip2 = pRecip2->next, iRecipient2--) {
				if (DecryptMessage(pbEncoded, cbEncoded, fFailBody, pEnveloped,
						pRecipients, iRecipient, pRecip2, iRecipient2)) {
					passCount++;
				}
			}
		}
	}
	return passCount > 0;

errorReturn:
	CFails += 1;
	return 0;
}

int ValidateEnveloped(const cn_cbor *pControl)
{
	int cbEncoded;
	byte *pbEncoded = GetCBOREncoding(pControl, &cbEncoded);

	return _ValidateEnveloped(pControl, pbEncoded, cbEncoded);
}

HCOSE_RECIPIENT BuildRecipient(const cn_cbor *pRecipient)
{
	HCOSE_RECIPIENT hRecip = COSE_Recipient_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hRecip == NULL) {
	returnError:
		COSE_Recipient_Free(hRecip);
		return NULL;
	}

	if (!SetSendingAttributes(
			(HCOSE)hRecip, pRecipient, Attributes_Recipient_protected)) {
		goto returnError;
	}

	cn_cbor *cnKey = cn_cbor_mapget_string(pRecipient, "key");
	if (cnKey != NULL) {
		cn_cbor *pkey = BuildKey(cnKey, true);
		if (pkey == NULL) {
			goto returnError;
		}

		if (!COSE_Recipient_SetKey(hRecip, pkey, NULL)) {
			goto returnError;
		}
	}

	cnKey = cn_cbor_mapget_string(pRecipient, "recipients");
	if (cnKey != NULL) {
		for (cnKey = cnKey->first_child; cnKey != NULL; cnKey = cnKey->next) {
			HCOSE_RECIPIENT hRecip2 = BuildRecipient(cnKey);
			if (hRecip2 == NULL) {
				goto returnError;
			}
			if (!COSE_Recipient_AddRecipient(hRecip, hRecip2, NULL)) {
				goto returnError;
			}
			COSE_Recipient_Free(hRecip2);
		}
	}

	cn_cbor *pSenderKey = cn_cbor_mapget_string(pRecipient, "sender_key");
	if (pSenderKey != NULL) {
		cn_cbor *pSendKey = BuildKey(pSenderKey, false);
		cn_cbor *pKid = cn_cbor_mapget_string(pSenderKey, "kid");
		if (!COSE_Recipient_SetSenderKey(
				hRecip, pSendKey, (pKid == NULL) ? 2 : 1, NULL)) {
			goto returnError;
		}
	}

#if INCLUDE_COUNTERSIGNATURE
	// On the Recipient
	cn_cbor *countersigns1 = cn_cbor_mapget_string(pRecipient, "countersign");
	if (countersigns1 != NULL) {
		countersigns1 = cn_cbor_mapget_string(countersigns1, "signers");
		cn_cbor *countersign = countersigns1->first_child;

		for (; countersign != NULL; countersign = countersign->next) {
			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(countersign, "key"), false);
			if (pkeyCountersign == NULL) {
				goto returnError;
			}

			HCOSE_COUNTERSIGN hCountersign =
				COSE_CounterSign_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
			if (hCountersign == NULL) {
				goto returnError;
			}

			if (!SetSendingAttributes((HCOSE)hCountersign, countersign,
					Attributes_Countersign_protected)) {
				goto returnError;
			}

			if (!COSE_CounterSign_SetKey(hCountersign, pkeyCountersign, NULL)) {
				goto returnError;
			}

			if (!COSE_Recipient_add_countersignature(
					hRecip, hCountersign, NULL)) {
				goto returnError;
			}

			COSE_CounterSign_Free(hCountersign);
		}
	}

#endif

	return hRecip;
}

int BuildEnvelopedMessage(const cn_cbor *pControl)
{
	int iRecipient;

	//
	//  We don't run this for all control sequences - skip those marked fail.
	//

	const cn_cbor *pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		return 0;
	}

	HCOSE_ENVELOPED hEncObj = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);

	const cn_cbor *pInputs = cn_cbor_mapget_string(pControl, "input");
	if (pInputs == NULL) {
	returnError:
		if (hEncObj != NULL) {
			COSE_Enveloped_Free(hEncObj);
		}

		CFails += 1;
		return 0;
	}
	const cn_cbor *pEnveloped = cn_cbor_mapget_string(pInputs, "enveloped");
	if (pEnveloped == NULL) {
		goto returnError;
	}

	const cn_cbor *pContent = cn_cbor_mapget_string(pInputs, "plaintext");
	if (!COSE_Enveloped_SetContent(
			hEncObj, pContent->v.bytes, pContent->length, NULL)) {
		goto returnError;
	}

	if (!SetSendingAttributes(
			(HCOSE)hEncObj, pEnveloped, Attributes_Enveloped_protected)) {
		goto returnError;
	}

	const cn_cbor *pAlg =
		COSE_Enveloped_map_get_int(hEncObj, 1, COSE_BOTH, NULL);
	if (pAlg == NULL) {
		goto returnError;
	}

	const cn_cbor *pRecipients =
		cn_cbor_mapget_string(pEnveloped, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) {
		goto returnError;
	}

	pRecipients = pRecipients->first_child;
	for (iRecipient = 0; pRecipients != NULL;
		 iRecipient++, pRecipients = pRecipients->next) {
		HCOSE_RECIPIENT hRecip = BuildRecipient(pRecipients);
		if (hRecip == NULL) {
			goto returnError;
		}

		if (!COSE_Enveloped_AddRecipient(hEncObj, hRecip, NULL)) {
			COSE_Recipient_Free(hRecip);
			goto returnError;
		}

		COSE_Recipient_Free(hRecip);
	}

#if INCLUDE_COUNTERSIGNATURE
	// On the Evneloped body
	cn_cbor *countersigns1 = cn_cbor_mapget_string(pEnveloped, "countersign");
	if (countersigns1 != NULL) {
		countersigns1 = cn_cbor_mapget_string(countersigns1, "signers");
		cn_cbor *countersign = countersigns1->first_child;

		for (; countersign != NULL; countersign = countersign->next) {
			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(countersign, "key"), false);
			if (pkeyCountersign == NULL) {
				goto returnError;
			}

			HCOSE_COUNTERSIGN hCountersign =
				COSE_CounterSign_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
			if (hCountersign == NULL) {
				goto returnError;
			}

			if (!SetSendingAttributes((HCOSE)hCountersign, countersign,
					Attributes_Countersign_protected)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			if (!COSE_CounterSign_SetKey(hCountersign, pkeyCountersign, NULL)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			if (!COSE_Enveloped_add_countersignature(
					hEncObj, hCountersign, NULL)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			COSE_CounterSign_Free(hCountersign);
		}
	}

#endif

	if (!COSE_Enveloped_encrypt(hEncObj, NULL)) {
		goto returnError;
	}

	size_t cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0) + 1;
	byte *rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);

	COSE_Enveloped_Free(hEncObj);

	int f = _ValidateEnveloped(pControl, rgb, cb);
	free(rgb);
	return f;
}

int EncryptMessage()
{
	HCOSE_ENVELOPED hEncObj = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	byte rgbSecret[128 / 8] = {'a', 'b', 'c'};
	int cbSecret = 128 / 8;
	byte rgbKid[15] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'l',
		'm', 'n', 'o', 'p'};
	int cbKid = 6;
	size_t cb;
	byte *rgb;
	const char *sz = "This is the content to be used";
	HCOSE_RECIPIENT hRecip = NULL;

	if (hEncObj == NULL) {
	errorReturn:
		if (hEncObj != NULL) {
			COSE_Enveloped_Free(hEncObj);
		}
		if (hRecip != NULL) {
			COSE_Recipient_Free(hRecip);
		}
		CFails++;
		return 0;
	}
	if (!COSE_Enveloped_map_put_int(hEncObj, COSE_Header_Algorithm,
			cn_cbor_int_create(COSE_Algorithm_AES_CCM_16_64_128,
				CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_PROTECT_ONLY, NULL)) {
		goto errorReturn;
	}
	if (!COSE_Enveloped_SetContent(hEncObj, (byte *)sz, strlen(sz), NULL)) {
		goto errorReturn;
	}
	if (!COSE_Enveloped_map_put_int(hEncObj, COSE_Header_IV,
			cn_cbor_data_create(rgbKid, 13, CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_UNPROTECT_ONLY, NULL)) {
		goto errorReturn;
	}

	hRecip = COSE_Recipient_from_shared_secret(
		rgbSecret, cbSecret, rgbKid, cbKid, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hRecip == NULL) {
		goto errorReturn;
	}
	if (!COSE_Enveloped_AddRecipient(hEncObj, hRecip, NULL)) {
		goto errorReturn;
	}

	if (!COSE_Enveloped_encrypt(hEncObj, NULL)) {
		goto errorReturn;
	}

	cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0);
	if (cb < 1) {
		goto errorReturn;
	}
	rgb = (byte *)malloc(cb);
	if (rgb == NULL) {
		goto errorReturn;
	}
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);
	if (cb < 1) {
		goto errorReturn;
	}

	COSE_Recipient_Free(hRecip);
	hRecip = NULL;

	FILE *fp = fopen("test.cbor", "wb");
	fwrite(rgb, cb, 1, fp);
	fclose(fp);

#if 0
	char * szX;
	int cbPrint = 0;
	cn_cbor * cbor = COSE_get_cbor((HCOSE) hEncObj);
	cbPrint = cn_cbor_printer_write(NULL, 0, cbor, "  ", "\r\n");
	szX = malloc(cbPrint);
	cn_cbor_printer_write(szX, cbPrint, cbor, "  ", "\r\n");
	fprintf(stdout, "%s", szX);
	fprintf(stdout, "\r\n");
#endif

	COSE_Enveloped_Free(hEncObj);
	hEncObj = NULL;

	/* */

	int typ;
	hEncObj = (HCOSE_ENVELOPED)COSE_Decode(rgb, (int)cb, &typ,
		COSE_enveloped_object, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hEncObj == NULL) {
		goto errorReturn;
	}

	int iRecipient = 0;
	do {
		hRecip = COSE_Enveloped_GetRecipient(hEncObj, iRecipient, NULL);
		if (hRecip == NULL) {
			break;
		}

		if (!COSE_Recipient_SetKey_secret(
				hRecip, rgbSecret, cbSecret, NULL, 0, NULL)) {
			goto errorReturn;
		}

		if (!COSE_Enveloped_decrypt(hEncObj, hRecip, NULL)) {
			goto errorReturn;
		}

		COSE_Recipient_Free(hRecip);
		hRecip = NULL;

		iRecipient += 1;

	} while (true);

	COSE_Enveloped_Free(hEncObj);
	return 1;
}
#endif

/********************************************/
#if INCLUDE_ENCRYPT0

int _ValidateEncrypt(const cn_cbor *pControl,
	const byte *pbEncoded,
	size_t cbEncoded,
	cn_cbor *pcnEncoded)
{
	const cn_cbor *pInput = cn_cbor_mapget_string(pControl, "input");
	const cn_cbor *pFail = NULL;
	const cn_cbor *pEncrypt = NULL;
	const cn_cbor *pRecipients = NULL;
	cn_cbor *pkey = NULL;	
	HCOSE_ENCRYPT hEnc = NULL;
	int type;
	bool fFail = false;
	bool fFailBody = false;
	bool fAlgSupport = true;

	if (false) {
	exitHere:
		if (hEnc != NULL) {
			COSE_Encrypt_Free(hEnc);
		}
		if (pkey != NULL) {
			CN_CBOR_FREE(pkey, context);
		}

		if (fAlgSupport) {
			if (fFailBody) {
				if (!fFail) {
					fFail = true;
				}
				else {
					fFail = false;
				}
			}
		}
		else {
			fFail = false;
		}

		if (fFail) {
			CFails += 1;
		}
		return fAlgSupport ? 1 : 0;

	returnError:
		if (hEnc != NULL) {
			COSE_Encrypt_Free(hEnc);
		}

		CFails += 1;
		return 0;		
	}

	pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		fFailBody = true;
	}

	if ((pInput == NULL) || (pInput->type != CN_CBOR_MAP)) {
		goto returnError;
	}
	pEncrypt = cn_cbor_mapget_string(pInput, "encrypted");
	if ((pEncrypt == NULL) || (pEncrypt->type != CN_CBOR_MAP)) {
		goto returnError;
	}

	pRecipients = cn_cbor_mapget_string(pEncrypt, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) {
		goto returnError;
	}

	pRecipients = pRecipients->first_child;

	if (pcnEncoded == NULL) {
		hEnc = (HCOSE_ENCRYPT)COSE_Decode(pbEncoded, cbEncoded, &type,
			COSE_encrypt_object, CBOR_CONTEXT_PARAM_COMMA NULL);
		if (hEnc == NULL) {
			if (fFailBody) {
				return 0;
			}
			else {
				goto returnError;
			}
		}
	}
	else {
		hEnc = COSE_Encrypt_Init_From_Object(
			pcnEncoded, CBOR_CONTEXT_PARAM_COMMA NULL);
		if (hEnc == NULL) {
			if (fFailBody) {
				return 0;
			}
			else {
				goto returnError;
			}
		}
	}

	if (!SetReceivingAttributes(
			(HCOSE)hEnc, pEncrypt, Attributes_Encrypt_protected)) {
		goto returnError;
	}

	pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), true);
	if (pkey == NULL) {
		goto returnError;
	}

	cn_cbor *k = cn_cbor_mapget_int(pkey, -1);
	if (k == NULL) {
		fFail = true;
		goto exitHere;
	}

	cn_cbor *alg =
		COSE_Encrypt_map_get_int(hEnc, COSE_Header_Algorithm, COSE_BOTH, NULL);
	if (!IsAlgorithmSupported(alg)) {
		fAlgSupport = false;
		fFail = false;
		goto exitHere;
	}

	pFail = cn_cbor_mapget_string(pRecipients, "fail");
	if (COSE_Encrypt_decrypt(hEnc, k->v.bytes, k->length, NULL)) {
		if (!fAlgSupport) {
			fFail = true;
			fAlgSupport = false;
		}
		else if ((pFail != NULL) && (pFail->type != CN_CBOR_TRUE)) {
			fFail = true;
		}

		size_t cb;
		const byte *pb;
		pb = COSE_Encrypt_GetContent(hEnc, &cb, NULL);
	}
	else {
		if (fAlgSupport) {
			fFail = true;
			fAlgSupport = false;
		}
		else if ((pFail == NULL) || (pFail->type == CN_CBOR_FALSE)) {
			fFail = true;
		}
	}

#if INCLUDE_COUNTERSIGNATURE
	//  Countersign on Encrypt0 Body

	//  Validate counter signatures on signers
	cn_cbor *countersignList = cn_cbor_mapget_string(pEncrypt, "countersign");
	if (countersignList != NULL) {
		cn_cbor *countersigners =
			cn_cbor_mapget_string(countersignList, "signers");
		if (countersigners == NULL) {
			fFail = true;
			goto exitHere;
		}
		int count = countersigners->length;
		bool forward = true;

		if (COSE_Encrypt_map_get_int(hEnc, COSE_Header_CounterSign,
				COSE_UNPROTECT_ONLY, 0) == NULL) {
			goto returnError;
		}

		for (int counterNo = 0; counterNo < count; counterNo++) {
			bool noSupportSign = false;
			bool failThis = false;

			HCOSE_COUNTERSIGN h =
				COSE_Encrypt0_get_countersignature(hEnc, counterNo, 0);
			if (h == NULL) {
				fFail = true;
				continue;
			}

			cn_cbor *counterSigner = cn_cbor_index(
				countersigners, forward ? counterNo : count - counterNo - 1);

			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(counterSigner, "key"), false);
			if (pkeyCountersign == NULL) {
				fFail = true;
				COSE_CounterSign_Free(h);
				continue;
			}

			if (!COSE_CounterSign_SetKey(h, pkeyCountersign, 0)) {
				fFail = true;
				COSE_CounterSign_Free(h);
				CN_CBOR_FREE(pkeyCountersign, context);
				continue;
			}

			alg = COSE_CounterSign_map_get_int(
				h, COSE_Header_Algorithm, COSE_BOTH, NULL);
			if (!IsAlgorithmSupported(alg)) {
				noSupportSign = true;
				fAlgSupport = false;
			}

			if (COSE_Encrypt0_CounterSign_validate(hEnc, h, 0)) {
				//  I don't think we have any forced errors yet.
			}
			else {
				if (forward && counterNo == 0 && count > 1) {
					forward = false;
					counterNo -= 1;
				}
				else {
					fFail = true;
				}
			}

			COSE_CounterSign_Free(h);
		}
	}
#endif

	goto exitHere;
}

int ValidateEncrypt(const cn_cbor *pControl)
{
	int cbEncoded;
	byte *pbEncoded = GetCBOREncoding(pControl, &cbEncoded);
	int fRet;

	fRet = _ValidateEncrypt(pControl, pbEncoded, cbEncoded, NULL);
	if (!fRet) {
		return fRet;
	}

	cn_cbor *cbor =
		cn_cbor_decode(pbEncoded, cbEncoded, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (cbor == NULL) {
		return false;
	}

	return _ValidateEncrypt(pControl, NULL, 0, cbor);
}

int BuildEncryptMessage(const cn_cbor *pControl)
{
	cn_cbor *pkey = NULL;
	//
	//  We don't run this for all control sequences - skip those marked fail.
	//

	const cn_cbor *pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		return 0;
	}

	HCOSE_ENCRYPT hEncObj =
		COSE_Encrypt_Init(COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);

	const cn_cbor *pInputs = cn_cbor_mapget_string(pControl, "input");
	if (pInputs == NULL) {
	returnError:
		if (hEncObj != NULL) {
			COSE_Encrypt_Free(hEncObj);
		}

		if (pkey != NULL) {
			CN_CBOR_FREE(pkey, context);
		}

		CFails += 1;
		return 1;
	}
	const cn_cbor *pEncrypt = cn_cbor_mapget_string(pInputs, "encrypted");
	if (pEncrypt == NULL) {
		goto returnError;
	}

	const cn_cbor *pContent = cn_cbor_mapget_string(pInputs, "plaintext");
	if (!COSE_Encrypt_SetContent(
			hEncObj, pContent->v.bytes, pContent->length, NULL)) {
		goto returnError;
	}

	if (!SetSendingAttributes(
			(HCOSE)hEncObj, pEncrypt, Attributes_Encrypt_protected)) {
		goto returnError;
	}

	const cn_cbor *pAlg = COSE_Encrypt_map_get_int(hEncObj, 1, COSE_BOTH, NULL);
	if (pAlg == NULL) {
		goto returnError;
	}

	const cn_cbor *pRecipients = cn_cbor_mapget_string(pEncrypt, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) {
		goto returnError;
	}

	pRecipients = pRecipients->first_child;
	pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), false);
	if (pkey == NULL) {
		goto returnError;
	}

	cn_cbor *k = cn_cbor_mapget_int(pkey, -1);

#if INCLUDE_COUNTERSIGNATURE
	// On the Encrypt0 body
	cn_cbor *countersigns = cn_cbor_mapget_string(pEncrypt, "countersign");
	if (countersigns != NULL) {
		countersigns = cn_cbor_mapget_string(countersigns, "signers");
		cn_cbor *countersign = countersigns->first_child;

		for (; countersign != NULL; countersign = countersign->next) {
			cn_cbor *pkeyCountersign =
				BuildKey(cn_cbor_mapget_string(countersign, "key"), false);
			if (pkeyCountersign == NULL) {
				goto returnError;
			}

			HCOSE_COUNTERSIGN hCountersign =
				COSE_CounterSign_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
			if (hCountersign == NULL) {
				goto returnError;
			}

			if (!SetSendingAttributes((HCOSE)hCountersign, countersign,
					Attributes_Countersign_protected)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			if (!COSE_CounterSign_SetKey(hCountersign, pkeyCountersign, NULL)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			if (!COSE_Encrypt0_add_countersignature(
					hEncObj, hCountersign, NULL)) {
				COSE_CounterSign_Free(hCountersign);
				goto returnError;
			}

			COSE_CounterSign_Free(hCountersign);
		}
	}

#endif

	if (!COSE_Encrypt_encrypt(hEncObj, k->v.bytes, k->length, NULL)) {
		goto returnError;
	}

	size_t cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0) + 1;
	byte *rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);

	COSE_Encrypt_Free(hEncObj);

	int f = _ValidateEncrypt(pControl, rgb, cb, NULL);

	if (pkey != NULL) {
		CN_CBOR_FREE(pkey, context);
	}
	
	free(rgb);
	return f;
}
#endif

#if INCLUDE_ENCRYPT
void Enveloped_Corners()
{
	HCOSE_ENVELOPED hEncryptNULL = NULL;
	HCOSE_ENVELOPED hEncrypt = NULL;
	HCOSE_ENVELOPED hEncryptBad = NULL;
	HCOSE_RECIPIENT hRecipientNULL = NULL;
	HCOSE_RECIPIENT hRecipient = NULL;
	HCOSE_RECIPIENT hRecipientBad = NULL;
	byte rgb[10];
	cn_cbor *cn = cn_cbor_int_create(5, CBOR_CONTEXT_PARAM_COMMA NULL);
	cose_errback cose_error;

	hEncrypt = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
#if INCLUDE_MAC
	hEncryptBad = (HCOSE_ENVELOPED)COSE_Mac_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
#else
	hEncryptBad = (HCOSE_ENVELOPED)COSE_CALLOC(1, sizeof(COSE), context);
#endif
	hRecipient = COSE_Recipient_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
#if INCLUDE_MAC
	hRecipientBad = (HCOSE_RECIPIENT)COSE_Mac_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
#else
	hRecipientBad = (HCOSE_RECIPIENT)COSE_CALLOC(1, sizeof(COSE), context);
#endif

	//  Missing case - addref then release on item

	//
	//  Do parameter checks
	//      - NULL handle
	//      - Incorrect handle
	//      - NULL pointer
	//

	CHECK_FAILURE(COSE_Enveloped_SetContent(hEncryptNULL, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_SetContent(hEncryptBad, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_SetContent(hEncrypt, NULL, 10, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Enveloped_map_put_int(
					  hEncryptNULL, 1, cn, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_map_put_int(
					  hEncryptBad, 1, cn, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_map_put_int(hEncrypt, 1, cn,
					  COSE_PROTECT_ONLY | COSE_UNPROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);
	CHECK_FAILURE(COSE_Enveloped_map_put_int(
					  hEncrypt, 1, NULL, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(
		COSE_Enveloped_map_get_int(hEncryptNULL, 1, COSE_BOTH, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(
		COSE_Enveloped_map_get_int(hEncryptBad, 1, COSE_BOTH, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE(COSE_Enveloped_encrypt(hEncryptNULL, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_encrypt(hEncryptBad, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE(COSE_Enveloped_decrypt(hEncryptNULL, hRecipient, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_decrypt(hEncryptBad, hRecipient, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_decrypt(hEncrypt, hRecipientNULL, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_decrypt(hEncrypt, hRecipientBad, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE(
		COSE_Enveloped_AddRecipient(hEncryptNULL, hRecipient, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(
		COSE_Enveloped_AddRecipient(hEncryptBad, hRecipient, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(
		COSE_Enveloped_AddRecipient(hEncrypt, hRecipientNULL, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(
		COSE_Enveloped_AddRecipient(hEncrypt, hRecipientBad, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE_PTR(COSE_Enveloped_GetRecipient(hEncryptNULL, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE_PTR(COSE_Enveloped_GetRecipient(hEncryptBad, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE(
		COSE_Enveloped_SetExternal(hEncryptNULL, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_SetExternal(hEncryptBad, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_SetExternal(hEncrypt, NULL, 10, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	if (!COSE_Enveloped_Free(hEncrypt)) {
		CFails++;
	}
	if (!COSE_Recipient_Free(hRecipient)) {
		CFails++;
	}

	//
	//  Unsupported algorithm

	//  Bad Int algorithm

	hEncrypt = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hEncrypt == NULL) {
		CFails++;
	}
	CHECK_RETURN(
		COSE_Enveloped_SetContent(hEncrypt, (byte *)"Message", 7, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Enveloped_map_put_int(hEncrypt, COSE_Header_Algorithm,
					 cn_cbor_int_create(-99, CBOR_CONTEXT_PARAM_COMMA NULL),
					 COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_NONE, CFails++);
	hRecipient = COSE_Recipient_from_shared_secret(
		rgb, sizeof(rgb), rgb, sizeof(rgb), CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hRecipient == NULL) {
		CFails++;
	}
	CHECK_RETURN(COSE_Enveloped_AddRecipient(hEncrypt, hRecipient, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_encrypt(hEncrypt, &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);
	COSE_Recipient_Free(hRecipient);
	COSE_Enveloped_Free(hEncrypt);

	hEncrypt = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hEncrypt == NULL) {
		CFails++;
	}
	CHECK_RETURN(
		COSE_Enveloped_SetContent(hEncrypt, (byte *)"Message", 7, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(
		COSE_Enveloped_map_put_int(hEncrypt, COSE_Header_Algorithm,
			cn_cbor_string_create("hmac", CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_PROTECT_ONLY, &cose_error),
		COE_ERR_NONE, CFails++);
	hRecipient = COSE_Recipient_from_shared_secret(
		rgb, sizeof(rgb), rgb, sizeof(rgb), CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hRecipient == NULL) {
		CFails++;
	}
	CHECK_RETURN(COSE_Enveloped_AddRecipient(hEncrypt, hRecipient, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_encrypt(hEncrypt, &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);

	//
	//  Over shoot the recipients

	CHECK_FAILURE_PTR(COSE_Enveloped_GetRecipient(hEncrypt, -1, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);
	CHECK_FAILURE_PTR(COSE_Enveloped_GetRecipient(hEncrypt, 9, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	COSE_Enveloped_Free(hEncrypt);
	COSE_Recipient_Free(hRecipient);

	return;
}
#endif

#if INCLUDE_ENCRYPT0
void Encrypt_Corners()
{
	HCOSE_ENCRYPT hEncrypt = NULL;
	byte rgb[10];
	cn_cbor *cn = cn_cbor_int_create(5, CBOR_CONTEXT_PARAM_COMMA NULL);
	cose_errback cose_error;

	//  Missing case - addref then release on item

	//  NULL Handle checks

	if (COSE_Encrypt_SetContent(hEncrypt, rgb, 10, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_map_get_int(hEncrypt, 1, COSE_BOTH, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_map_put_int(hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_SetExternal(hEncrypt, rgb, 10, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_encrypt(hEncrypt, rgb, sizeof(rgb), NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_decrypt(hEncrypt, rgb, sizeof(rgb), NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_Free((HCOSE_ENCRYPT)hEncrypt)) {
		CFails++;
	}

	//  Wrong type of handle checks

#if INCLUDE_MAC
	hEncrypt = (HCOSE_ENCRYPT)COSE_Mac_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
#else
	hEncrypt = (HCOSE_ENCRYPT)COSE_CALLOC(1, sizeof(COSE), context);
#endif

	if (COSE_Encrypt_SetContent(hEncrypt, rgb, 10, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_map_get_int(hEncrypt, 1, COSE_BOTH, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_map_put_int(hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_encrypt(hEncrypt, rgb, sizeof(rgb), NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_SetExternal(hEncrypt, rgb, 10, NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_decrypt(hEncrypt, rgb, sizeof(rgb), NULL)) {
		CFails++;
	}
	if (COSE_Encrypt_Free(hEncrypt)) {
		CFails++;
	}

	//
	//  Unsupported algorithm

	hEncrypt =
		COSE_Encrypt_Init(COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hEncrypt == NULL) {
		CFails++;
	}
	if (!COSE_Encrypt_SetContent(hEncrypt, (byte *)"Message", 7, NULL)) {
		CFails++;
	}
	if (!COSE_Encrypt_map_put_int(hEncrypt, COSE_Header_Algorithm,
			cn_cbor_int_create(-99, CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_PROTECT_ONLY, NULL)) {
		CFails++;
	}
	CHECK_FAILURE(COSE_Encrypt_encrypt(hEncrypt, rgb, sizeof(rgb), &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);
	COSE_Encrypt_Free(hEncrypt);

	hEncrypt =
		COSE_Encrypt_Init(COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hEncrypt == NULL) {
		CFails++;
	}
	if (!COSE_Encrypt_SetContent(hEncrypt, (byte *)"Message", 7, NULL)) {
		CFails++;
	}
	if (!COSE_Encrypt_map_put_int(hEncrypt, COSE_Header_Algorithm,
			cn_cbor_int_create(-99, CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_PROTECT_ONLY, NULL)) {
		CFails++;
	}
	CHECK_FAILURE(COSE_Encrypt_encrypt(hEncrypt, rgb, sizeof(rgb), &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);
	COSE_Encrypt_Free(hEncrypt);

	return;
}
#endif

#if INCLUDE_ENCRYPT || INCLUDE_MAC
void Recipient_Corners()
{
	HCOSE_RECIPIENT hRecip;
	HCOSE_RECIPIENT hRecipNULL = NULL;
	HCOSE_RECIPIENT hRecipBad;
	cose_errback cose_error;
	byte rgb[10];
	cn_cbor *cn = cn_cbor_int_create(1, CBOR_CONTEXT_PARAM_COMMA NULL);

	hRecip = COSE_Recipient_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA & cose_error);
#if INCLUDE_SIGN
	hRecipBad = (HCOSE_RECIPIENT)COSE_Signer_Init(
		CBOR_CONTEXT_PARAM_COMMA & cose_error);
#else
	hRecipBad = (HCOSE_RECIPIENT)COSE_CALLOC(1, sizeof(COSE), context);
#endif

	//  Check for invalid parameters

	CHECK_FAILURE_PTR(COSE_Recipient_from_shared_secret(NULL, 0, NULL, 0,
						  CBOR_CONTEXT_PARAM_COMMA & cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Recipient_SetKey_secret(
					  hRecipNULL, rgb, sizeof(rgb), NULL, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetKey_secret(
					  hRecipBad, rgb, sizeof(rgb), NULL, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetKey_secret(
					  hRecip, NULL, sizeof(rgb), NULL, 0, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Recipient_SetKey(hRecipNULL, cn, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetKey(hRecipBad, cn, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetKey(hRecip, NULL, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Recipient_SetSenderKey(hRecipNULL, cn, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetSenderKey(hRecipBad, cn, 0, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetSenderKey(hRecip, NULL, 0, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetSenderKey(hRecip, cn, 3, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);
	CHECK_RETURN(COSE_Recipient_SetSenderKey(hRecip, cn, 0, &cose_error),
		COSE_ERR_NONE, CFails++);

	CHECK_FAILURE(COSE_Recipient_SetExternal(hRecipNULL, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_SetExternal(hRecipBad, rgb, 10, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	CHECK_FAILURE(
		COSE_Recipient_map_get_int(hRecipNULL, 1, COSE_BOTH, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(
		COSE_Recipient_map_get_int(hRecipBad, 1, COSE_BOTH, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_map_get_int(hRecip, 1, COSE_BOTH, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Recipient_map_put_int(
					  hRecipNULL, 1, cn, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_map_put_int(
					  hRecipBad, 1, cn, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_map_put_int(
					  hRecip, 1, NULL, COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);
	CHECK_FAILURE(COSE_Recipient_map_put_int(hRecip, 1, cn,
					  COSE_PROTECT_ONLY | COSE_UNPROTECT_ONLY, &cose_error),
		COSE_ERR_INVALID_PARAMETER, CFails++);

	CHECK_FAILURE(COSE_Recipient_AddRecipient(hRecipNULL, hRecip, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_AddRecipient(hRecipBad, hRecip, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_AddRecipient(hRecip, hRecipNULL, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);
	CHECK_FAILURE(COSE_Recipient_AddRecipient(hRecip, hRecipBad, &cose_error),
		COSE_ERR_INVALID_HANDLE, CFails++);

	COSE_Recipient_Free(hRecip);

	//  Unknown algorithms
#if INCLUDE_ENCRYPT
	HCOSE_ENVELOPED hEnv = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	hRecip = COSE_Recipient_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);

	CHECK_RETURN(COSE_Enveloped_map_put_int(hEnv, COSE_Header_Algorithm,
					 cn_cbor_int_create(COSE_Algorithm_AES_GCM_128,
						 CBOR_CONTEXT_PARAM_COMMA NULL),
					 COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Enveloped_SetContent(
					 hEnv, (byte *)"This the body", 13, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Recipient_map_put_int(hRecip, COSE_Header_Algorithm,
					 cn_cbor_int_create(-99, CBOR_CONTEXT_PARAM_COMMA NULL),
					 COSE_UNPROTECT_ONLY, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Enveloped_AddRecipient(hEnv, hRecip, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_encrypt(hEnv, &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);

	COSE_Enveloped_Free(hEnv);
	COSE_Recipient_Free(hRecip);

	hEnv = COSE_Enveloped_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);
	hRecip = COSE_Recipient_Init(
		COSE_INIT_FLAGS_NONE, CBOR_CONTEXT_PARAM_COMMA NULL);

	CHECK_RETURN(COSE_Enveloped_map_put_int(hEnv, COSE_Header_Algorithm,
					 cn_cbor_int_create(COSE_Algorithm_AES_GCM_128,
						 CBOR_CONTEXT_PARAM_COMMA NULL),
					 COSE_PROTECT_ONLY, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Enveloped_SetContent(
					 hEnv, (byte *)"This the body", 13, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(
		COSE_Recipient_map_put_int(hRecip, COSE_Header_Algorithm,
			cn_cbor_string_create("Unknown", CBOR_CONTEXT_PARAM_COMMA NULL),
			COSE_UNPROTECT_ONLY, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_RETURN(COSE_Enveloped_AddRecipient(hEnv, hRecip, &cose_error),
		COSE_ERR_NONE, CFails++);
	CHECK_FAILURE(COSE_Enveloped_encrypt(hEnv, &cose_error),
		COSE_ERR_UNKNOWN_ALGORITHM, CFails++);

	COSE_Enveloped_Free(hEnv);
	COSE_Recipient_Free(hRecip);
#endif
}
#endif
