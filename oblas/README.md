# oblas

This is commit https://github.com/sleepybishop/oblas/commit/63196b91b1d9d9e5f125ddf03a204c33b5a5eb72 plus AXV512 backport.

blas-like routines to solve systems in finite fields [gf2, gf256]

The table generator `tablegen.c` also supports [gf4, gf16] but routines to work with packed vectors in those fields is incomplete.

#### Optimizing for different archs
 - NEON: `make CPPFLAGS+="-DOBLAS_NEON"`
 - SSE: `make CPPFLAGS+="-DOBLAS_SSE"`
 - AVX: `make CPPFLAGS+="-DOBLAS_AVX -DOCTMAT_ALIGN=32"`
 - AVX512: `make CPPFLAGS+="-DOBLAS_AVX -DOCTMAT_ALIGN=64"`

#### Customizing
Edit `tablegen.c` to change polynomial/field size.

