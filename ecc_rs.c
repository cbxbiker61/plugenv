/* This program is an encoder/decoder for Reed-Solomon codes. Encoding is in
   systematic form, decoding via the Berlekamp iterative algorithm.
   In the present form , the constants mm, nn, tt, and kk=nn-2tt must be
   specified  (the double letters are used simply to avoid clashes with
   other n,k,t used in other programs into which this was incorporated!)
   Also, the irreducible polynomial used to generate GF(2**mm) must also be
   entered -- these can be found in Lin and Costello, and also Clark and Cain.

   The representation of the elements of GF(2**m) is either in index form,
   where the number is the power of the primitive element alpha, which is
   convenient for multiplication (add the powers modulo 2**m-1) or in
   polynomial form, where the bits represent the coefficients of the
   polynomial representation of the number, which is the most convenient form
   for addition.  The two forms are swapped between via lookup tables.
   This leads to fairly messy looking expressions, but unfortunately, there
   is no easy alternative when working with Galois arithmetic.

   The code is not written in the most elegant way, but to the best
   of my knowledge, (no absolute guarantees!), it works.
   However, when including it into a simulation program, you may want to do
   some conversion of global variables (used here because I am lazy!) to
   local variables where appropriate, and passing parameters (eg array
   addresses) to the functions  may be a sensible move to reduce the number
   of global variables and thus decrease the chance of a bug being introduced.

   This program does not handle erasures at present, but should not be hard
   to adapt to do this, as it is just an adjustment to the Berlekamp-Massey
   algorithm. It also does not attempt to decode past the BCH bound -- see
   Blahut "Theory and practice of error control codes" for how to do this.

              Simon Rockliff, University of Adelaide   21/9/89

   26/6/91 Slight modifications to remove a compiler dependent bug which hadn't
           previously surfaced. A few extra comments added for clarity.
           Appears to all work fine, ready for posting to net!

                  Notice
                 --------
   This program may be freely modified and/or given to whoever wants it.
   A condition of such distribution is that the author's contribution be
   acknowledged by his name being left in the comments heading the program,
   however no responsibility is accepted for any financial or other loss which
   may result from some unforseen errors or malfunctioning of the program
   during use.
                                 Simon Rockliff, 26th June 1991
 */

#include "ecc_rs.h"
#include <stdint.h>

#define mm 10	  /* RS code over GF(2**mm) - the size in bits of a symbol*/
#define	nn 1023   /* nn=2^mm -1   length of codeword */
#define tt 4      /* number of errors that can be corrected */
#define kk 1015   /* kk = number of information symbols  kk = nn-2*tt  */


static char rs_initialized = 0;

typedef unsigned int gf;
typedef unsigned short u_short;
typedef u_short dtype;
typedef u_short tgf;  /* data type of Galois Functions */

/* Primitive polynomials -  irriducibile polynomial  [ 1+x^3+x^10 ]*/
static short pp[mm+1] = { 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1 };


/* index->polynomial form conversion table */
static tgf alpha_to[nn + 1];

/* Polynomial->index form conversion table */
static tgf index_of[nn + 1];

/* Generator polynomial g(x) = 2*tt with roots @, @^2, .. ,@^(2*tt) */
static tgf Gg[nn - kk + 1];


#define	minimum(a,b)	((a) < (b) ? (a) : (b))

#define	BLANK(a,n) {				\
		short ci;			\
		for(ci=0; ci<(n); ci++)		\
			(a)[ci] = 0;		\
	}

#define	COPY(a,b,n) {				\
		short ci;			\
		for(ci=(n)-1;ci >=0;ci--)	\
			(a)[ci] = (b)[ci];	\
	}
#define	COPYDOWN(a,b,n) {			\
		short ci;			\
		for(ci=(n)-1;ci >=0;ci--)	\
			(a)[ci] = (b)[ci];	\
	}


/* generate GF(2^m) from the irreducible polynomial p(X) in p[0]..p[mm]
   lookup tables:  index->polynomial form   alpha_to[] contains j=alpha^i;
   polynomial form -> index form  index_of[j=alpha^i] = i
   alpha=2 is the primitive element of GF(2^m)
*/

static void generate_gf(void)
{
	register int i, mask;

	mask = 1;
	alpha_to[mm] = 0;
	for (i = 0; i < mm; i++) {
		alpha_to[i] = mask;
		index_of[alpha_to[i]] = i;
		if (pp[i] != 0)
			alpha_to[mm] ^= mask;
		mask <<= 1;
	}
	index_of[alpha_to[mm]] = mm;

	mask >>= 1;
	for (i = mm + 1; i < nn; i++) {
		if (alpha_to[i - 1] >= mask)
			alpha_to[i] = alpha_to[mm] ^ ((alpha_to[i - 1] ^ mask) << 1);
		else
			alpha_to[i] = alpha_to[i - 1] << 1;
		index_of[alpha_to[i]] = i;
	}
	index_of[0] = nn;
	alpha_to[nn] = 0;
}


/*
 * Obtain the generator polynomial of the tt-error correcting,
 * length nn = (2^mm -1)
 * Reed Solomon code from the product of (X + @^i), i=1..2*tt
 */
static void gen_poly(void)
{
	register int i, j;

	Gg[0] = alpha_to[1]; /* primitive element*/
	Gg[1] = 1;		     /* g(x) = (X+@^1) initially */
	for (i = 2; i <= nn - kk; i++) {
		Gg[i] = 1;
		/*
		 * Below multiply (Gg[0]+Gg[1]*x + ... +Gg[i]x^i) by
		 * (@^i + x)
		 */
		for (j = i - 1; j > 0; j--)
			if (Gg[j] != 0)
				Gg[j] = Gg[j - 1] ^ alpha_to[((index_of[Gg[j]]) + i)%nn];
			else
				Gg[j] = Gg[j - 1];
		Gg[0] = alpha_to[((index_of[Gg[0]]) + i) % nn];
	}
	/* convert Gg[] to index form for quicker encoding */
	for (i = 0; i <= nn - kk; i++)
		Gg[i] = index_of[Gg[i]];
}

/*
 * take the string of symbols in data[i], i=0..(k-1) and encode
 * systematically to produce nn-kk parity symbols in bb[0]..bb[nn-kk-1] data[]
 * is input and bb[] is output in polynomial form. Encoding is done by using
 * a feedback shift register with appropriate connections specified by the
 * elements of Gg[], which was generated above. Codeword is   c(X) =
 * data(X)*X**(nn-kk)+ b(X)
 */
static char encode_rs(dtype data[kk], dtype bb[nn-kk])
{
	register int i, j;
	tgf feedback;

	BLANK(bb,nn-kk);
	for (i = kk - 1; i >= 0; i--) {
		if(data[i] > nn)
			return -1;	/* Illegal symbol */
		feedback = index_of[data[i] ^ bb[nn - kk - 1]];
		if (feedback != nn) {	/* feedback term is non-zero */
			for (j = nn - kk - 1; j > 0; j--)
				if (Gg[j] != nn)
					bb[j] = bb[j - 1] ^ alpha_to[(Gg[j] + feedback)%nn];
				else
					bb[j] = bb[j - 1];
			bb[0] = alpha_to[(Gg[0] + feedback)%nn];
		} else {
			for (j = nn - kk - 1; j > 0; j--)
				bb[j] = bb[j - 1];
			bb[0] = 0;
		}
	}
	return 0;
}

/* assume we have received bits grouped into mm-bit symbols in data[i],
   i=0..(nn-1), We first compute the 2*tt syndromes, then we use the
   Berlekamp iteration to find the error location polynomial  elp[i].
   If the degree of the elp is >tt, we cannot correct all the errors
   and hence just put out the information symbols uncorrected. If the
   degree of elp is <=tt, we  get the roots, hence the inverse roots,
   the error location numbers. If the number of errors located does not
   equal the degree of the elp, we have more than tt errors and cannot
   correct them.  Otherwise, we then solve for the error value at the
   error location and correct the error.The procedure is that found in
   Lin and Costello.*/

static int decode_rs(dtype data[nn])
{
	int deg_lambda, el, deg_omega;
	int i, j, r;
	tgf q,tmp,num1,num2,den,discr_r;
	tgf recd[nn];
	tgf lambda[nn-kk + 1], s[nn-kk + 1];	/* Err+Eras Locator poly
						 * and syndrome poly  */
	tgf b[nn-kk + 1], t[nn-kk + 1], omega[nn-kk + 1];
	tgf root[nn-kk], reg[nn-kk + 1], loc[nn-kk];
	int syn_error, count;

	/* data[] is in polynomial form, copy and convert to index form */
	for (i = nn-1; i >= 0; i--){

		if(data[i] > nn)
			return -1;	/* Illegal symbol */

		recd[i] = index_of[data[i]];
	}

	/* first form the syndromes; i.e., evaluate recd(x) at roots of g(x)
	 * namely @**(1+i), i = 0, ... ,(nn-kk-1)
	 */

	syn_error = 0;

	for (i = 1; i <= nn-kk; i++) {
		tmp = 0;

		for (j = 0; j < nn; j++)
			if (recd[j] != nn)	/* recd[j] in index form */
				tmp ^= alpha_to[(recd[j] + (1+i-1)*j)%nn];
		syn_error |= tmp;	/* set flag if non-zero syndrome =>
					 * error */
		/* store syndrome in index form  */
		s[i] = index_of[tmp];
	}

	if (!syn_error) {
		/*
		 * if syndrome is zero, data[] is a codeword and there are no
		 * errors to correct. So return data[] unmodified
		 */
		return 0;
	}

	BLANK(&lambda[1],nn-kk);

	lambda[0] = 1;

	for(i=0;i<nn-kk+1;i++)
		b[i] = index_of[lambda[i]];

	/*
	 * Begin Berlekamp-Massey algorithm to determine error
	 * locator polynomial
	 */
	r = 0;
	el = 0;
	while (++r <= nn-kk) {	/* r is the step number */
		/* Compute discrepancy at the r-th step in poly-form */
		discr_r = 0;

		for (i = 0; i < r; i++){
			if ((lambda[i] != 0) && (s[r - i] != nn)) {
				discr_r ^= alpha_to[(index_of[lambda[i]] + s[r - i])%nn];
			}
		}

		discr_r = index_of[discr_r];	/* Index form */
		if (discr_r == nn) {
			/* 2 lines below: B(x) <-- x*B(x) */
			COPYDOWN(&b[1],b,nn-kk);
			b[0] = nn;
		} else {
			/* 7 lines below: T(x) <-- lambda(x) - discr_r*x*b(x) */
			t[0] = lambda[0];
			for (i = 0 ; i < nn-kk; i++) {
				if(b[i] != nn)
					//t[i+1] = lambda[i+1] ^ alpha_to[modnn(discr_r + b[i])];
					t[i+1] = lambda[i+1] ^ alpha_to[(discr_r + b[i])%nn];
				else
					t[i+1] = lambda[i+1];
			}
			if (2 * el <= r - 1) {
				el = r - el;
				/*
				 * 2 lines below: B(x) <-- inv(discr_r) *
				 * lambda(x)
				 */
				for (i = 0; i <= nn-kk; i++)
					//b[i] = (lambda[i] == 0) ? nn : modnn(index_of[lambda[i]] - discr_r + nn);
					b[i] = (lambda[i] == 0) ? nn : ((index_of[lambda[i]] - discr_r + nn)%nn);
			} else {
				/* 2 lines below: B(x) <-- x*B(x) */
				COPYDOWN(&b[1],b,nn-kk);
				b[0] = nn;
			}
			COPY(lambda,t,nn-kk+1);
		}
	}

	/* Convert lambda to index form and compute deg(lambda(x)) */
	deg_lambda = 0;
	for(i=0;i<nn-kk+1;i++){
		lambda[i] = index_of[lambda[i]];
		if(lambda[i] != nn)
			deg_lambda = i;
	}
	/*
	 * Find roots of the error locator polynomial. By Chien
	 * Search
	 */
	COPY(&reg[1],&lambda[1],nn-kk);
	count = 0;		/* Number of roots of lambda(x) */
	for (i = 1; i <= nn; i++) {
		q = 1;
		for (j = deg_lambda; j > 0; j--)
			if (reg[j] != nn) {
				//reg[j] = modnn(reg[j] + j);
				reg[j] = (reg[j] + j)%nn;
				q ^= alpha_to[reg[j]];
			}
		if (!q) {
			/* store root (index-form) and error location number */
			root[count] = i;
			loc[count] = nn - i;
			count++;
		}
	}

#ifdef DEBUG
/*
  printf("\n Final error positions:\t");
  for (i = 0; i < count; i++)
  printf("%d ", loc[i]);
  printf("\n");
*/
#endif

	if (deg_lambda != count) {
		/*
		 * deg(lambda) unequal to number of roots => uncorrectable
		 * error detected
		 */
		return -1;
	}
	/*
	 * Compute err evaluator poly omega(x) = s(x)*lambda(x) (modulo
	 * x**(nn-kk)). in index form. Also find deg(omega).
	 */

	deg_omega = 0;
	for (i = 0; i < nn-kk;i++){
		tmp = 0;
		j = (deg_lambda < i) ? deg_lambda : i;
		for(;j >= 0; j--){
			if ((s[i + 1 - j] != nn) && (lambda[j] != nn))
				//tmp ^= alpha_to[modnn(s[i + 1 - j] + lambda[j])];
				tmp ^= alpha_to[(s[i + 1 - j] + lambda[j])%nn];
		}
		if(tmp != 0)
			deg_omega = i;
		omega[i] = index_of[tmp];
	}
	omega[nn-kk] = nn;

	/*
	 * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
	 * inv(X(l))**(1-1) and den = lambda_pr(inv(X(l))) all in poly-form
	 */
	for (j = count-1; j >=0; j--) {
		num1 = 0;
		for (i = deg_omega; i >= 0; i--) {
			if (omega[i] != nn)
				//num1  ^= alpha_to[modnn(omega[i] + i * root[j])];
				num1  ^= alpha_to[(omega[i] + i * root[j])%nn];
		}
		//num2 = alpha_to[modnn(root[j] * (1 - 1) + nn)];
		num2 = alpha_to[(root[j] * (1 - 1) + nn)%nn];
		den = 0;

		/* lambda[i+1] for i even is the formal derivative lambda_pr of lambda[i] */
		for (i = minimum(deg_lambda,nn-kk-1) & ~1; i >= 0; i -=2) {
			if(lambda[i+1] != nn)
				//den ^= alpha_to[modnn(lambda[i+1] + i * root[j])];
				den ^= alpha_to[(lambda[i+1] + i * root[j])%nn];
		}
		if (den == 0) {
#ifdef DEBUG
			printf("\n ERROR: denominator = 0\n");
#endif
			return -1;
		}
		/* Apply error to data */
		if (num1 != 0) {
			//data[loc[j]] ^= alpha_to[modnn(index_of[num1] + index_of[num2] + nn - index_of[den])];
			data[loc[j]] ^= alpha_to[(index_of[num1] + index_of[num2] + nn - index_of[den])%nn];
		}
	}
	return count;
}

/**
 * calculate_ecc_rs - Calculate 10 byte Reed-Solomon ECC code for 512 byte block
 * @dat:	raw data
 * @ecc_code:	buffer for ECC
 */
extern int calculate_ecc_rs(const uint8_t *data, uint8_t *ecc_code)
{
	int i;
	u_short rsdata[nn];

	/* Generate Tables in first run */
	if (!rs_initialized) {
		generate_gf();
		gen_poly();
		rs_initialized = 1;
	}

	for(i=512; i<nn; i++)
		rsdata[i] = 0;

	for(i=0; i<512; i++)
		rsdata[i] = (u_short) data[i];
	if ((encode_rs(rsdata,&(rsdata[kk]))) != 0)
		return -1;
	*(ecc_code)	= (unsigned char) rsdata[kk];
	*(ecc_code+1)	= ((rsdata[0x3F7])   >> 8) | ((rsdata[0x3F7+1]) << 2);
	*(ecc_code+2)	= ((rsdata[0x3F7+1]) >> 6) | ((rsdata[0x3F7+2]) << 4);
	*(ecc_code+3)	= ((rsdata[0x3F7+2]) >> 4) | ((rsdata[0x3F7+3]) << 6);
	*(ecc_code+4)	= ((rsdata[0x3F7+3]) >> 2);
	*(ecc_code+5)	= (unsigned char) rsdata[kk+4];
	*(ecc_code+6)	= ((rsdata[0x3F7+4])   >> 8) | ((rsdata[0x3F7+1+4]) << 2);
	*(ecc_code+7)	= ((rsdata[0x3F7+1+4]) >> 6) | ((rsdata[0x3F7+2+4]) << 4);
	*(ecc_code+8)	= ((rsdata[0x3F7+2+4]) >> 4) | ((rsdata[0x3F7+3+4]) << 6);
	*(ecc_code+9)	= ((rsdata[0x3F7+3+4]) >> 2);

	return 0;
}

/**
 * correct_data_rs - Detect and correct bit error(s) using 10-byte Reed-Solomon ECC
 * @dat:	raw data read from the chip
 * @store_ecc:	ECC from the chip
 * @calc_ecc:	the ECC calculated from raw data
 */
extern int correct_data_rs(uint8_t *data, uint8_t *store_ecc, uint8_t *calc_ecc)
{
	int ret,i;
	u_short rsdata[nn];

	/* Generate Tables in first run */
	if (!rs_initialized) {
		generate_gf();
		gen_poly();
		rs_initialized = 1;
	}

	/* is decode needed ? */
	if (	(*(uint16_t*)store_ecc       == *(uint16_t*)calc_ecc)       &&
		(*(uint16_t*)(store_ecc + 2) == *(uint16_t*)(calc_ecc + 2)) &&
		(*(uint16_t*)(store_ecc + 4) == *(uint16_t*)(calc_ecc + 4)) &&
		(*(uint16_t*)(store_ecc + 6) == *(uint16_t*)(calc_ecc + 6)) &&
		(*(uint16_t*)(store_ecc + 8) == *(uint16_t*)(calc_ecc + 8)))
	{
		return 0;
	}
	/* did we read an erased page ? */
	for(i = 0; i < 512 ;i += 4)
	{
		if(*(uint32_t*)(data+i) != 0xFFFFFFFF)
		{
			goto correct;
		}
	}

	/* page was erased, return gracefully */
	return 0;


correct:
	for(i=512; i<nn; i++) rsdata[i] = 0;

	/* errors*/
	//data[20] = 0xDD;
	//data[30] = 0xDD;
	//data[40] = 0xDD;
	//data[50] = 0xDD;
	//data[60] = 0xDD;

	/* Ecc is calculated on chunks of 512B */
	for(i=0; i<512; i++)
		rsdata[i] = (u_short) data[i];

	rsdata[kk]   = ( (*(store_ecc+1) & (unsigned char)0x03) <<8) | (*(store_ecc));
	rsdata[kk+1] = ( (*(store_ecc+2) & (unsigned char)0x0F) <<6) | (*(store_ecc+1)>>2);
	rsdata[kk+2] = ( (*(store_ecc+3) & (unsigned char)0x3F) <<4) | (*(store_ecc+2)>>4);
	rsdata[kk+3] = (*(store_ecc+4) <<2) | (*(store_ecc+3)>>6);

	rsdata[kk+4] = ( (*(store_ecc+1+5) & 0x03) <<8) | (*(store_ecc+5));
	rsdata[kk+5] = ( (*(store_ecc+2+5) & 0x0F) <<6) | (*(store_ecc+1+5)>>2);
	rsdata[kk+6] = ( (*(store_ecc+3+5) & 0x3F) <<4) | (*(store_ecc+2+5)>>4);
	rsdata[kk+7] = (*(store_ecc+4+5) <<2) | (*(store_ecc+3+5)>>6);

	ret = decode_rs(rsdata);

	/* Check for excessive errors */
	if ((ret > tt) || (ret < 0))
		return -1;

	/* Copy corrected data */
	for (i=0; i<512; i++)
		data[i] = (unsigned char) rsdata[i];

	return 0;
}

