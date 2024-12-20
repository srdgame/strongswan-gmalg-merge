/*
 * Copyright (C) 2008-2013 Tobias Brunner
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */


#include <gmalg.h>

#include "gmalg_ec_diffie_hellman.h"

#include <utils/debug.h>

typedef struct private_gmalg_ec_diffie_hellman_t private_gmalg_ec_diffie_hellman_t;

/**
 * Private data of an gmalg_ec_diffie_hellman_t object.
 */
struct private_gmalg_ec_diffie_hellman_t {
	/**
	 * Public gmalg_ec_diffie_hellman_t interface.
	 */
	gmalg_ec_diffie_hellman_t public;

	/**
	 * Diffie Hellman group.
	 */
	key_exchange_method_t group;

	/**
	 * Shared secret
	 */
	chunk_t shared_secret;

	/**
	 * the cipher device handle
	 */
	void *hDeviceHandle;

	ECCrefPublicKey pubKey[1];
	ECCrefPrivateKey priKey[1];
	ECCrefPublicKey otherPubKey[1];

	/**
	 * True if shared secret is computed
	 */
	bool computed;
};

/**
 * Compute the shared secret.
 *
 * We cannot use the function ECDH_compute_key() because that returns only the
 * x coordinate of the shared secret point (which is defined, for instance, in
 * 'NIST SP 800-56A').
 * However, we need both coordinates as RFC 4753 says: "The Diffie-Hellman
 *   public value is obtained by concatenating the x and y values. The format
 *   of the Diffie-Hellman shared secret value is the same as that of the
 *   Diffie-Hellman public value."
 */
static bool compute_shared_key(private_gmalg_ec_diffie_hellman_t *this,
							   chunk_t *shared_secret)
{
	ECCrefPublicKey P[1];
	bool ret = TRUE;

	*shared_secret = chunk_alloc(ECCref_MAX_LEN*2);
	GMALG_pointMul_ECC(this->hDeviceHandle, this->otherPubKey, this->priKey, P);
	memcpy(shared_secret->ptr, P->x, ECCref_MAX_LEN);
	memcpy(shared_secret->ptr + ECCref_MAX_LEN, P->y, ECCref_MAX_LEN);

	return ret;
}

METHOD(key_exchange_t, set_public_key, bool,
	private_gmalg_ec_diffie_hellman_t *this, chunk_t value)
{
	chunk_clear(&this->shared_secret);

	memcpy(this->otherPubKey->x, value.ptr, ECCref_MAX_LEN);
	memcpy(this->otherPubKey->y, value.ptr + ECCref_MAX_LEN, ECCref_MAX_LEN);

	if (!compute_shared_key(this, &this->shared_secret)) {
		DBG1(DBG_LIB, "ECDH shared secret computation failed");
		return FALSE;
	}

	this->computed = TRUE;
	return TRUE;
}

METHOD(key_exchange_t, get_public_key, bool,
	private_gmalg_ec_diffie_hellman_t *this,chunk_t *value)
{
	*value  = chunk_alloc(ECCref_MAX_LEN*2);
	memcpy(value->ptr, this->pubKey->x, ECCref_MAX_LEN);
	memcpy(value->ptr + ECCref_MAX_LEN, this->pubKey->y, ECCref_MAX_LEN);

	return TRUE;
}

METHOD(key_exchange_t, set_private_key, bool,
	private_gmalg_ec_diffie_hellman_t *this, chunk_t value)
{
	bool ret = FALSE;
	if (value.len != ECCref_MAX_LEN)
		DBG1(DBG_LIB, "SM2 set private value failed");

	memcpy(this->priKey->K, value.ptr, ECCref_MAX_LEN);

	return ret;
}

METHOD(key_exchange_t, get_shared_secret, bool,
	private_gmalg_ec_diffie_hellman_t *this, chunk_t *secret)
{
	if (!this->computed)
	{
		return FALSE;
	}
	*secret = chunk_clone(this->shared_secret);
	return TRUE;
}

METHOD(key_exchange_t, get_method, key_exchange_method_t,
	private_gmalg_ec_diffie_hellman_t *this)
{
	return this->group;
}

METHOD(key_exchange_t, destroy, void,
	private_gmalg_ec_diffie_hellman_t *this)
{
	GMALG_CloseDevice(this->hDeviceHandle);
	chunk_clear(&this->shared_secret);
	free(this);
}


/*
 * Described in header.
 */
gmalg_ec_diffie_hellman_t *gmalg_ec_diffie_hellman_create(key_exchange_method_t group)
{
	private_gmalg_ec_diffie_hellman_t *this;

	INIT(this,
		.public = {
			.ke = {
				.get_shared_secret = _get_shared_secret,
				.set_public_key = _set_public_key,
				.get_public_key = _get_public_key,
				.set_private_key = _set_private_key,
				.get_method = _get_method,
				.destroy = _destroy,
			},
		},
		.group = group,
	);

	if (group != CURVE_SM2)
	{
		free(this);
		return NULL;

	}

	GMALG_OpenDevice(&this->hDeviceHandle);
	GMALG_GenerateKeyPair_ECC(this->hDeviceHandle, this->pubKey, this->priKey);

	return &this->public;
}
