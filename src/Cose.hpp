#pragma once

#include <cose/cose.h>
#include <cn-cbor/cn-cbor.h>

// forward declarations
class COSE_CounterSign;
class COSE_CounterSign1;

class COSE {
   public:
	//  Not sure what goes here yet
	COSE_INIT_FLAGS m_flags{COSE_INIT_FLAGS_NONE};
	//  Do I own the pointer @ m_cbor?
	bool m_ownMsg{false};
	//  Do I own the pointer @ m_unportectedMap?
	bool m_ownUnprotectedMap{false};
	//  What message type is this?
	int m_msgType{};
	//  Allocator Reference Counting.
	int m_refCount{};
	cn_cbor *m_cbor{nullptr};
	cn_cbor *m_cborRoot{nullptr};
	cn_cbor *m_protectedMap{nullptr};
	cn_cbor *m_unprotectMap{nullptr};
	cn_cbor *m_dontSendMap{nullptr};
	const byte *m_pbExternal{nullptr};
	size_t m_cbExternal{0};
#ifdef USE_CBOR_CONTEXT
	cn_cbor_context m_allocContext;
#endif
	COSE *m_handleList{nullptr};
#if INCLUDE_COUNTERSIGNATURE
	// Linked list of all counter signatures
	COSE_CounterSign *m_counterSigners{nullptr};
#endif
#if INCLUDE_COUNTERSIGNATURE1
	COSE_CounterSign1 *m_counterSign1{nullptr};
#endif
};

bool _COSE_Init(COSE_INIT_FLAGS flags,
	COSE *pcose,
	int msgType,
	CBOR_CONTEXT_COMMA cose_errback *perr);
bool _COSE_Init_From_Object(COSE *pobj,
	cn_cbor *pcbor,
	CBOR_CONTEXT_COMMA cose_errback *perr);
void _COSE_Release(COSE *pcose);
