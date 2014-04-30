/* mpz_powm_ui(res,base,exp,mod) -- Set R to (B^E) mod M.

   Contributed to the GNU project by Torbjörn Granlund.

Copyright 1991, 1993, 1994, 1996, 1997, 2000-2002, 2005, 2008, 2009, 2011-2013
Free Software Foundation, Inc.

This file is part of the GNU MP Library.

The GNU MP Library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the GNU MP Library.  If not,
see https://www.gnu.org/licenses/.  */


#include "mpir.h"
#include "gmp-impl.h"
#include "longlong.h"


/* This code is very old, and should be rewritten to current GMP standard.  It
   is slower than mpz_powm for large exponents, but also for small exponents
   when the mod argument is small.

   As an intermediate solution, we now deflect to mpz_powm for exponents >= 20.
*/

/*
  b ^ e mod m   res
  0   0     0    ?
  0   e     0    ?
  0   0     m    ?
  0   e     m    0
  b   0     0    ?
  b   e     0    ?
  b   0     m    1 mod m
  b   e     m    b^e mod m
*/

static void
mod (mp_ptr np, mp_size_t nn, mp_srcptr dp, mp_size_t dn, mp_limb_t dinv,
     mp_limb_t d1inv, mp_ptr tp)
{
  mp_ptr qp;
  TMP_DECL;
  TMP_MARK;

  qp = tp;

  if (dn == 1)
    {
      np[0] = mpn_divrem_1 (qp, (mp_size_t) 0, np, nn, dp[0]); /* JPF */
    }
  else if (dn == 2)
    {
      mpn_divrem_2 (qp, 0L, np, nn, dp);
    }
  else if (BELOW_THRESHOLD (dn, DC_DIV_QR_THRESHOLD) ||
	   BELOW_THRESHOLD (nn - dn, DC_DIV_QR_THRESHOLD))
    {
      mpn_sb_div_qr (qp, np, nn, dp, dn, dinv, d1inv); /* JPF: no gmp_pi1_t, two limbs pi */
    }
  /* Different conditions */
  else if (BELOW_THRESHOLD (dn, INV_DIV_QR_THRESHOLD) ||   /* fast condition */
	   BELOW_THRESHOLD (nn, 2 * INV_DIV_QR_THRESHOLD)) /* fast condition */
    {
      mpn_dc_div_qr (qp, np, nn, dp, dn, dinv, d1inv); /* JPF: no gmp_pi1_t */
    }
  else
    {
      mp_ptr dinv2 = TMP_ALLOC_LIMBS(dn); /* JPF: ... */
      mpn_invert(dinv2, dp, dn); /* JPF: ... */
      mpn_inv_div_qr (qp, np, nn, dp, dn, dinv2); /* JPF: need nn+1 for new np? I don't think so as everyting is already normalized */
    }

  TMP_FREE;
}

/* Compute t = a mod m, a is defined by (ap,an), m is defined by (mp,mn), and
   t is defined by (tp,mn).  */
static void
reduce (mp_ptr tp, mp_srcptr ap, mp_size_t an, mp_srcptr mp, mp_size_t mn,
        mp_limb_t dinv, mp_limb_t d1inv)
{
  mp_ptr rp, scratch;
  TMP_DECL;
  TMP_MARK;

  rp = TMP_ALLOC_LIMBS (an);
  scratch = TMP_ALLOC_LIMBS (an - mn + 1);
  MPN_COPY (rp, ap, an);
  mod (rp, an, mp, mn, dinv, d1inv, scratch);
  MPN_COPY (tp, rp, mn);

  TMP_FREE;
}

void
mpz_powm_ui (mpz_ptr r, mpz_srcptr b, mpir_ui el, mpz_srcptr m)
{
  if (el < 20) /* JPF */
    {
      mp_ptr xp, tp, mp, bp, scratch;
      mp_size_t xn, tn, mn, bn;
      int m_zero_cnt;
      int c;
      mp_limb_t e, m2;
      mp_limb_t dinv, d1inv;
      TMP_DECL;

      mp = PTR(m);
      mn = ABSIZ(m);
      if (UNLIKELY (mn == 0))
	DIVIDE_BY_ZERO;

      if (el == 0)
	{
	  /* Exponent is zero, result is 1 mod M, i.e., 1 or 0 depending on if
	     M equals 1.  */
	  SIZ(r) = (mn == 1 && mp[0] == 1) ? 0 : 1;
	  PTR(r)[0] = 1;
	  return;
	}

      TMP_MARK;

      /* Normalize m (i.e. make its most significant bit set) as required by
	 division functions below.  */
      count_leading_zeros (m_zero_cnt, mp[mn - 1]);
      m_zero_cnt -= GMP_NAIL_BITS;
      if (m_zero_cnt != 0)
	{
	  mp_ptr new_mp = TMP_ALLOC_LIMBS (mn);
	  mpn_lshift (new_mp, mp, mn, m_zero_cnt);
	  mp = new_mp;
	}

      m2 = mn == 1 ? 0 : mp[mn - 2];
      mpir_invert_pi2 (dinv, d1inv, mp[mn - 1], m2); /* JPF: don't use gmp_pi1_t */

      bn = ABSIZ(b);
      bp = PTR(b);
      if (bn > mn)
	{
	  /* Reduce possibly huge base.  Use a function call to reduce, since we
	     don't want the quotient allocation to live until function return.  */
	  mp_ptr new_bp = TMP_ALLOC_LIMBS (mn);
	  reduce (new_bp, bp, bn, mp, mn, dinv, d1inv); /* JPF */
	  bp = new_bp;
	  bn = mn;
	  /* Canonicalize the base, since we are potentially going to multiply with
	     it quite a few times.  */
	  MPN_NORMALIZE (bp, bn);
	}

      if (bn == 0)
	{
	  SIZ(r) = 0;
	  TMP_FREE;
	  return;
	}

      tp = TMP_ALLOC_LIMBS (2 * mn + 1);
      xp = TMP_ALLOC_LIMBS (mn);
      scratch = TMP_ALLOC_LIMBS (mn + 1);

      MPN_COPY (xp, bp, bn);
      xn = bn;

      e = el;
      count_leading_zeros (c, e);
      e = (e << c) << 1;		/* shift the exp bits to the left, lose msb */
      c = GMP_LIMB_BITS - 1 - c;

      if (c == 0)
	{
	  /* If m is already normalized (high bit of high limb set), and b is
	     the same size, but a bigger value, and e==1, then there's no
	     modular reductions done and we can end up with a result out of
	     range at the end. */
	  if (xn == mn && mpn_cmp (xp, mp, mn) >= 0)
	    mpn_sub_n (xp, xp, mp, mn);
	}
      else
	{
	  /* Main loop. */
	  do
	    {
	      mpn_sqr (tp, xp, xn);
	      tn = 2 * xn; tn -= tp[tn - 1] == 0;
	      if (tn < mn)
		{
		  MPN_COPY (xp, tp, tn);
		  xn = tn;
		}
	      else
		{
                    mod (tp, tn, mp, mn, dinv, d1inv, scratch); /* JPF */
		  MPN_COPY (xp, tp, mn);
		  xn = mn;
		}

	      if ((mp_limb_signed_t) e < 0)
		{
		  mpn_mul (tp, xp, xn, bp, bn);
		  tn = xn + bn; tn -= tp[tn - 1] == 0;
		  if (tn < mn)
		    {
		      MPN_COPY (xp, tp, tn);
		      xn = tn;
		    }
		  else
		    {
                        mod (tp, tn, mp, mn, dinv, d1inv, scratch); /* JPF */
		      MPN_COPY (xp, tp, mn);
		      xn = mn;
		    }
		}
	      e <<= 1;
	      c--;
	    }
	  while (c != 0);
	}

      /* We shifted m left m_zero_cnt steps.  Adjust the result by reducing it
	 with the original M.  */
      if (m_zero_cnt != 0)
	{
	  mp_limb_t cy;
	  cy = mpn_lshift (tp, xp, xn, m_zero_cnt);
	  tp[xn] = cy; xn += cy != 0;

	  if (xn < mn)
	    {
	      MPN_COPY (xp, tp, xn);
	    }
	  else
	    {
                mod (tp, xn, mp, mn, dinv, d1inv, scratch); /* JPF */
	      MPN_COPY (xp, tp, mn);
	      xn = mn;
	    }
	  mpn_rshift (xp, xp, xn, m_zero_cnt);
	}
      MPN_NORMALIZE (xp, xn);

      if ((el & 1) != 0 && SIZ(b) < 0 && xn != 0)
	{
	  mp = PTR(m);			/* want original, unnormalized m */
	  mpn_sub (xp, mp, mn, xp, xn);
	  xn = mn;
	  MPN_NORMALIZE (xp, xn);
	}
      MPZ_REALLOC (r, xn);
      SIZ (r) = xn;
      MPN_COPY (PTR(r), xp, xn);

      TMP_FREE;
    }
  else /* e >= 20 */
    {
      /* For large exponents, fake a mpz_t exponent and deflect to the more
	 sophisticated mpz_powm.  */
      mpz_t e;
      mp_limb_t ep[LIMBS_PER_UI]; /* JPF: no ulong in MPIR */
      MPZ_FAKE_UI (e, ep, el);
      mpz_powm (r, b, e, m);
    }
}
