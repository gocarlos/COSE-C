#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cose.h>
#include <cn-cbor/cn-cbor.h>

#include "json.h"
#include "test.h"
#include "context.h"

int _ValidateMAC(const cn_cbor * pControl, const byte * pbEncoded, size_t cbEncoded)
{
	const cn_cbor * pInput = cn_cbor_mapget_string(pControl, "input");
	const cn_cbor * pFail;
	const cn_cbor * pMac;
	const cn_cbor * pRecipients;
	HCOSE_MAC hMAC;
	int type;
	int iRecipient;
	bool fFail = false;
	bool fFailBody = false;

	pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		fFailBody = true;
	}

	hMAC = (HCOSE_MAC) COSE_Decode(pbEncoded, cbEncoded, &type, COSE_mac_object, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hMAC == NULL) goto failTest;

	if ((pInput == NULL) || (pInput->type != CN_CBOR_MAP)) goto failTest;
	pMac = cn_cbor_mapget_string(pInput, "mac");
	if ((pMac == NULL) || (pMac->type != CN_CBOR_MAP)) goto failTest;

	pRecipients = cn_cbor_mapget_string(pMac, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) goto failTest;

	iRecipient = (int) pRecipients->length - 1;
	pRecipients = pRecipients->first_child;
	for (; pRecipients != NULL; iRecipient--, pRecipients=pRecipients->next) {
		cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), false);
		if (pkey == NULL) {
			fFail = true;
			continue;
		}

		HCOSE_RECIPIENT hRecip = COSE_Mac_GetRecipient(hMAC, iRecipient, NULL);
		if (hRecip == NULL) {
			fFail = true;
			continue;
		}

		if (!SetReceivingAttributes((HCOSE)hRecip, pRecipients, Attributes_Recipient_protected)) goto failTest;

		if (!COSE_Recipient_SetKey(hRecip, pkey, NULL)) {
			fFail = true;
			continue;
		}

		pFail = cn_cbor_mapget_string(pRecipients, "fail");
		if (COSE_Mac_validate(hMAC, hRecip, NULL)) {
			if ((pFail != NULL) && (pFail->type != CN_CBOR_TRUE)) fFail = true;
		}
		else {
			if ((pFail == NULL) || (pFail->type == CN_CBOR_FALSE)) fFail = true;
		}

		COSE_Recipient_Free(hRecip);
	}

	COSE_Mac_Free(hMAC);

	if (fFailBody) {
		if (!fFail) fFail = true;
		else fFail = false;
	}

	if (fFail) CFails += 1;
	return 0;

failTest:
	CFails += 1;
	return 0;
}

int ValidateMAC(const cn_cbor * pControl)
{
	int cbEncoded;
	byte * pbEncoded = GetCBOREncoding(pControl, &cbEncoded);

	return _ValidateMAC(pControl, pbEncoded, cbEncoded);
}

int BuildMacMessage(const cn_cbor * pControl)
{
	int iRecipient;

	//
	//  We don't run this for all control sequences - skip those marked fail.
	//

	const cn_cbor * pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) return 0;

	HCOSE_MAC hMacObj = COSE_Mac_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

	const cn_cbor * pInputs = cn_cbor_mapget_string(pControl, "input");
	if (pInputs == NULL) goto returnError;
	const cn_cbor * pMac = cn_cbor_mapget_string(pInputs, "mac");
	if (pMac == NULL) goto returnError;

	const cn_cbor * pContent = cn_cbor_mapget_string(pInputs, "plaintext");
	if (!COSE_Mac_SetContent(hMacObj, pContent->v.bytes, pContent->length, NULL)) goto returnError;

	if (!SetSendingAttributes((HCOSE)hMacObj, pMac, Attributes_MAC_protected)) goto returnError;

	const cn_cbor * pAlg = COSE_Mac_map_get_int(hMacObj, 1, COSE_BOTH, NULL);

	const cn_cbor * pRecipients = cn_cbor_mapget_string(pMac, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) goto returnError;

	pRecipients = pRecipients->first_child;
	for (iRecipient = 0; pRecipients != NULL; iRecipient++, pRecipients = pRecipients->next) {
		cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), true);
		if (pkey == NULL) goto returnError;

		HCOSE_RECIPIENT hRecip = COSE_Recipient_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
		if (hRecip == NULL) goto returnError;

		if (!SetSendingAttributes((HCOSE) hRecip, pRecipients, Attributes_Recipient_protected)) goto returnError;

		if (!COSE_Recipient_SetKey(hRecip, pkey, NULL))goto returnError;

		if (!COSE_Mac_AddRecipient(hMacObj, hRecip, NULL)) goto returnError;

		COSE_Recipient_Free(hRecip);
	}

	if (!COSE_Mac_encrypt(hMacObj, NULL)) goto returnError;

	size_t cb = COSE_Encode((HCOSE)hMacObj, NULL, 0, 0) + 1;
	byte * rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hMacObj, rgb, 0, cb);

	COSE_Mac_Free(hMacObj);

	int f = _ValidateMAC(pControl, rgb, cb);

	free(rgb);
	return f;

returnError:
	CFails += 1;
	return 1;
}

int MacMessage()
{
	HCOSE_MAC hEncObj = COSE_Mac_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
	char * sz = "This is the content to be used";
	byte rgbSecret[256 / 8] = { 'a', 'b', 'c' };
	byte  rgbKid[6] = { 'a', 'b', 'c', 'd', 'e', 'f' };
	int cbKid = 6;
	size_t cb;
	byte * rgb;

	COSE_Mac_map_put_int(hEncObj, COSE_Header_Algorithm, cn_cbor_int_create(COSE_Algorithm_HMAC_256_256, CBOR_CONTEXT_PARAM_COMMA NULL), COSE_PROTECT_ONLY, NULL);
	COSE_Mac_SetContent(hEncObj, (byte *) sz, strlen(sz), NULL);

	HCOSE_RECIPIENT hRecip = COSE_Recipient_from_shared_secret(rgbSecret, sizeof(rgbSecret), rgbKid, cbKid, CBOR_CONTEXT_PARAM_COMMA NULL);
	COSE_Mac_AddRecipient(hEncObj, hRecip, NULL);

	COSE_Mac_encrypt(hEncObj, NULL);

	cb = COSE_Encode((HCOSE)hEncObj, NULL, 0, 0) + 1;
	rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hEncObj, rgb, 0, cb);

	COSE_Mac_Free(hEncObj);

	FILE * fp = fopen("test.mac.cbor", "wb");
	fwrite(rgb, cb, 1, fp);
	fclose(fp);

#if 0
	char * szX;
	int cbPrint = 0;
	cn_cbor * cbor = COSE_get_cbor((HCOSE)hEncObj);
	cbPrint = cn_cbor_printer_write(NULL, 0, cbor, "  ", "\r\n");
	szX = malloc(cbPrint);
	cn_cbor_printer_write(szX, cbPrint, cbor, "  ", "\r\n");
	fprintf(stdout, "%s", szX);
	fprintf(stdout, "\r\n");
#endif

	/* */

	int typ;
	hEncObj = (HCOSE_MAC) COSE_Decode(rgb,  (int) cb, &typ, COSE_mac_object, CBOR_CONTEXT_PARAM_COMMA NULL);

	int iRecipient = 0;
	do {
		HCOSE_RECIPIENT hRecip2;

		hRecip2 = COSE_Mac_GetRecipient(hEncObj, iRecipient, NULL);
		if (hRecip2 == NULL) break;

		COSE_Recipient_SetKey_secret(hRecip2, rgbSecret, sizeof(rgbSecret), NULL);

		COSE_Mac_validate(hEncObj, hRecip2, NULL);

		iRecipient += 1;

		COSE_Recipient_Free(hRecip2);

	} while (true);

	COSE_Mac_Free(hEncObj);

	return 1;
}


int _ValidateMac0(const cn_cbor * pControl, const byte * pbEncoded, size_t cbEncoded)
{
	const cn_cbor * pInput = cn_cbor_mapget_string(pControl, "input");
	const cn_cbor * pFail;
	const cn_cbor * pMac;
	const cn_cbor * pRecipients;
	HCOSE_MAC0 hMAC;
	int type;
	bool fFail = false;
	bool fFailBody = false;

	pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) {
		fFailBody = true;
	}

	hMAC = (HCOSE_MAC0)COSE_Decode(pbEncoded, cbEncoded, &type, COSE_mac0_object, CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hMAC == NULL) goto errorReturn;

	if ((pInput == NULL) || (pInput->type != CN_CBOR_MAP)) goto errorReturn;
	pMac = cn_cbor_mapget_string(pInput, "mac0");
	if ((pMac == NULL) || (pMac->type != CN_CBOR_MAP)) goto errorReturn;

	pRecipients = cn_cbor_mapget_string(pMac, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) goto errorReturn;

	pRecipients = pRecipients->first_child;

	cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), true);
		if (pkey == NULL) {
			fFail = true;
			goto exitHere;
		}

		cn_cbor *k = cn_cbor_mapget_int(pkey, -1);

		pFail = cn_cbor_mapget_string(pRecipients, "fail");
		if (COSE_Mac0_validate(hMAC, k->v.bytes, k->length, NULL)) {
			if ((pFail != NULL) && (pFail->type != CN_CBOR_TRUE)) fFail = true;
		}
		else {
			if ((pFail == NULL) || (pFail->type == CN_CBOR_FALSE)) fFail = true;
		}


	COSE_Mac0_Free(hMAC);

	if (fFailBody) {
		if (!fFail) fFail = true;
		else fFail = false;
	}
	exitHere:
	if (fFail) CFails += 1;
	return 0;

errorReturn:
	CFails += 1;
	return 0;
}

int ValidateMac0(const cn_cbor * pControl)
{
	int cbEncoded;
	byte * pbEncoded = GetCBOREncoding(pControl, &cbEncoded);

	return _ValidateMac0(pControl, pbEncoded, cbEncoded);
}

int BuildMac0Message(const cn_cbor * pControl)
{

	//
	//  We don't run this for all control sequences - skip those marked fail.
	//

	const cn_cbor * pFail = cn_cbor_mapget_string(pControl, "fail");
	if ((pFail != NULL) && (pFail->type == CN_CBOR_TRUE)) return 0;

	HCOSE_MAC0 hMacObj = COSE_Mac0_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

	const cn_cbor * pInputs = cn_cbor_mapget_string(pControl, "input");
	if (pInputs == NULL) goto returnError;
	const cn_cbor * pMac = cn_cbor_mapget_string(pInputs, "mac0");
	if (pMac == NULL) goto returnError;

	const cn_cbor * pContent = cn_cbor_mapget_string(pInputs, "plaintext");
	if (!COSE_Mac0_SetContent(hMacObj, pContent->v.bytes, pContent->length, NULL)) goto returnError;

	if (!SetSendingAttributes((HCOSE)hMacObj, pMac, Attributes_MAC0_protected)) goto returnError;

	const cn_cbor * pAlg = COSE_Mac0_map_get_int(hMacObj, 1, COSE_BOTH, NULL);

	const cn_cbor * pRecipients = cn_cbor_mapget_string(pMac, "recipients");
	if ((pRecipients == NULL) || (pRecipients->type != CN_CBOR_ARRAY)) goto returnError;

	pRecipients = pRecipients->first_child;

	cn_cbor * pkey = BuildKey(cn_cbor_mapget_string(pRecipients, "key"), false);
		if (pkey == NULL) goto returnError;

		cn_cbor * k = cn_cbor_mapget_int(pkey, -1);

	if (!COSE_Mac0_encrypt(hMacObj, k->v.bytes, k->length, NULL)) goto returnError;

	size_t cb = COSE_Encode((HCOSE)hMacObj, NULL, 0, 0) + 1;
	byte * rgb = (byte *)malloc(cb);
	cb = COSE_Encode((HCOSE)hMacObj, rgb, 0, cb);

	COSE_Mac0_Free(hMacObj);

	int f = _ValidateMac0(pControl, rgb, cb);

	free(rgb);
	return f;

returnError:
	CFails += 1;
	return 1;
}


void MAC_Corners()
{
    HCOSE_MAC hMAC;
    HCOSE_ENCRYPT hEncrypt = NULL;
	HCOSE_RECIPIENT hRecipient = NULL;
    byte rgb[10];
	cn_cbor * cn = cn_cbor_int_create(5, CBOR_CONTEXT_PARAM_COMMA NULL);

    //  Missing case - addref then release on item

    //  Incorrect algorithm
    
    hMAC = (HCOSE_MAC) COSE_Mac_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

    //  Invalid Handle checks

    COSE_Mac_Free((HCOSE_MAC) hEncrypt);
    COSE_Mac_SetContent((HCOSE_MAC)hEncrypt, rgb, 10, NULL);
    COSE_Mac_map_get_int((HCOSE_MAC)hEncrypt, 1, COSE_BOTH, NULL);
	COSE_Mac_map_put_int((HCOSE_MAC)hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL);
    COSE_Mac_encrypt((HCOSE_MAC)hEncrypt, NULL);
    COSE_Mac_validate((HCOSE_MAC)hEncrypt, (HCOSE_RECIPIENT)hMAC, NULL);
    COSE_Mac_AddRecipient((HCOSE_MAC)hEncrypt, (HCOSE_RECIPIENT)hMAC, NULL);
    COSE_Mac_GetRecipient((HCOSE_MAC)hEncrypt, 0, NULL);

    hEncrypt = COSE_Encrypt_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

    COSE_Mac_Free((HCOSE_MAC)hEncrypt);
    COSE_Mac_SetContent((HCOSE_MAC)hEncrypt, rgb, 10, NULL);
    COSE_Mac_map_get_int((HCOSE_MAC)hEncrypt, 1, COSE_BOTH, NULL);
	COSE_Mac_map_put_int((HCOSE_MAC)hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL);
	COSE_Mac_encrypt((HCOSE_MAC)hEncrypt, NULL);
    COSE_Mac_validate((HCOSE_MAC)hEncrypt, (HCOSE_RECIPIENT)hMAC, NULL);
    COSE_Mac_AddRecipient((HCOSE_MAC)hEncrypt, (HCOSE_RECIPIENT)hMAC, NULL);
    COSE_Mac_GetRecipient((HCOSE_MAC)hEncrypt, 0, NULL);

    //
	//  Unsupported algorithm

	hMAC = COSE_Mac_Init(CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hMAC == NULL) CFails++;
	if (!COSE_Mac_SetContent(hMAC, (byte *) "Message", 7, NULL)) CFails++;
	if (!COSE_Mac_map_put_int(hMAC, COSE_Header_Algorithm, cn_cbor_int_create(-99, CBOR_CONTEXT_PARAM_COMMA NULL), COSE_PROTECT_ONLY, NULL)) CFails++;
	hRecipient = COSE_Recipient_from_shared_secret(rgb, sizeof(rgb), rgb, sizeof(rgb), CBOR_CONTEXT_PARAM_COMMA NULL);
	if (hRecipient == NULL) CFails++;
	if (!COSE_Mac_AddRecipient(hMAC, hRecipient, NULL)) CFails++;
	if (COSE_Mac_encrypt(hMAC, NULL)) CFails++;

	if (!COSE_Mac_map_put_int(hMAC, COSE_Header_Algorithm, cn_cbor_string_create("hmac", CBOR_CONTEXT_PARAM_COMMA NULL), COSE_PROTECT_ONLY, NULL)) CFails++;
	if (COSE_Mac_encrypt(hMAC, NULL)) CFails++;

    return;
}

void MAC0_Corners()
{
	HCOSE_ENCRYPT hEncrypt = NULL;
	byte rgb[10];
	cn_cbor * cn = cn_cbor_int_create(5, CBOR_CONTEXT_PARAM_COMMA NULL);

	hEncrypt = COSE_Encrypt_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

	//  Missing case - addref then release on item

	//  Invalid Handle checks

	COSE_Mac0_SetContent((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);
	COSE_Mac0_map_get_int((HCOSE_MAC0)hEncrypt, 1, COSE_BOTH, NULL);
	COSE_Mac0_map_put_int((HCOSE_MAC0)hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL);
	COSE_Mac0_encrypt((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);
	COSE_Mac0_validate((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);

	hEncrypt = COSE_Encrypt_Init(CBOR_CONTEXT_PARAM_COMMA NULL);

	COSE_Mac0_Free((HCOSE_MAC0)hEncrypt);
	COSE_Mac0_SetContent((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);
	COSE_Mac0_map_get_int((HCOSE_MAC0)hEncrypt, 1, COSE_BOTH, NULL);
	COSE_Mac0_map_put_int((HCOSE_MAC0)hEncrypt, 1, cn, COSE_PROTECT_ONLY, NULL);
	COSE_Mac0_encrypt((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);
	COSE_Mac0_validate((HCOSE_MAC0)hEncrypt, rgb, 10, NULL);

	COSE_Mac0_Free((HCOSE_MAC0)hEncrypt);



	return;
}
