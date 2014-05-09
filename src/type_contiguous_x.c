#include "bigmpi_impl.h"

/* This function does all the heavy lifting in BigMPI. */

#ifdef BIGMPI_AVOID_TYPE_CREATE_STRUCT
#include <math.h>
/*
 * Synopsis
 *
 * int BigMPI_Factorize_count(MPI_Count c, int * a, int *b)
 *
 *  Input Parameter
 *
 *   c                  large count
 *
 * Output Parameters
 *
 *   a, b               integers such that c=a*b and a,b<INT_MAX
 *   rc                 returns 0 if a,b found (success), else 1 (failure)
 *
 */
static int BigMPI_Factorize_count(MPI_Count in, int * a, int *b)
{
#ifdef BIGMPI_DEBUG
    int debug = 1;
#else
    int debug = 0;
#endif

    /* THIS FUNCTION IS NOT OPTIMIZED AND MAY RUN VERY SLOWLY IN MANY CASES */
    /* TODO Implement something other than brute-force search for prime factors. */

    /* Is it better to do the division as MPI_Count or double? */
    MPI_Count lo = in/bigmpi_int_max+1;
    MPI_Count hi = (MPI_Count)floor(sqrt((double)in));

    if (debug) printf("(lo,hi) = (%zu,%zu)\n", (size_t)lo, (size_t)hi);
    for (MPI_Count g=lo; g<hi; g++) {
        MPI_Count rem = in%g;
        if (debug) printf("in=%zu, g=%zu, mod(in,g)=%zu\n", (size_t)in, (size_t)g, (size_t)rem);
        if (rem==0) {
            *a = (int)g;
            *b = (int)(in/g);
            if (debug) printf("a=%d, b=%d\n", *a, *b);
            return 0;
        }
    }
    if (debug) printf("failed to find valid factorization of %zu...\n",(size_t)in);
    return 1;
}
#endif

/*
 * Synopsis
 *
 * int MPIX_Type_contiguous_x(MPI_Count count,
 *                            MPI_Datatype   oldtype,
 *                            MPI_Datatype * newtype)
 *
 *  Input Parameters
 *
 *   count             replication count (nonnegative integer)
 *   oldtype           old datatype (handle)
 *
 * Output Parameter
 *
 *   newtype           new datatype (handle)
 *
 */
int MPIX_Type_contiguous_x(MPI_Count count, MPI_Datatype oldtype, MPI_Datatype * newtype)
{
    /* The count has to fit into MPI_Aint for BigMPI to work. */
    assert(count<bigmpi_count_max);

#ifdef BIGMPI_AVOID_TYPE_CREATE_STRUCT
    int a, b;
    int notprime = BigMPI_Factorize_count(count, &a, &b);
    if (notprime) {
        MPI_Type_vector(a, b, b, oldtype, newtype);
        return MPI_SUCCESS;
    }
#endif
    MPI_Count c = count/bigmpi_int_max;
    MPI_Count r = count%bigmpi_int_max;

    MPI_Datatype chunks;
    MPI_Type_vector(c, bigmpi_int_max, bigmpi_int_max, oldtype, &chunks);

    MPI_Datatype remainder;
    MPI_Type_contiguous(r, oldtype, &remainder);

    MPI_Aint lb /* unused */, extent;
    MPI_Type_get_extent(oldtype, &lb, &extent);

    MPI_Aint remdisp          = (MPI_Aint)c*bigmpi_int_max*extent;
    int blocklengths[2]       = {1,1};
    MPI_Aint displacements[2] = {0,remdisp};
    MPI_Datatype types[2]     = {chunks,remainder};
    MPI_Type_create_struct(2, blocklengths, displacements, types, newtype);

    MPI_Type_free(&chunks);
    MPI_Type_free(&remainder);

    return MPI_SUCCESS;
}

/*
 * Synopsis
 *
 * This function inverts MPIX_Type_contiguous_x, i.e. it provides
 * the original arguments for that call so that we know how many
 * built-in types are in the user-defined datatype.
 *
 * This function is primary used inside of BigMPI and does not
 * correspond to an MPI function, so we do avoid the use of the
 * MPIX namespace.
 *
 * int BigMPI_Decode_contiguous_x(MPI_Datatype   intype,
 *                                MPI_Count    * count,
 *                                MPI_Datatype * basetype)
 *
 *  Input Parameters
 *
 *   newtype           new datatype (handle)
 *
 * Output Parameter
 *
 *   count             replication count (nonnegative integer)
 *   oldtype           old datatype (handle)
 *
 */
int BigMPI_Decode_contiguous_x(MPI_Datatype intype, MPI_Count * count, MPI_Datatype * basetype)
{
    int nint, nadd, ndts, combiner;

    /* Step 1: Decode the type_create_struct call. */

    MPI_Type_get_envelope(intype, &nint, &nadd, &ndts, &combiner);
    assert(combiner==MPI_COMBINER_CONTIGUOUS || combiner==MPI_COMBINER_VECTOR);
#ifdef BIGMPI_AVOID_TYPE_CREATE_STRUCT
    if (combiner==MPI_COMBINER_VECTOR) {
        assert(nint==3);
        assert(nadd==0);
        assert(ndts==1);

        int cbs[3]; /* {count,blocklength,stride} */
        MPI_Datatype vbasetype[1];
        MPI_Type_get_contents(intype, 3, 0, 1, cbs, NULL, vbasetype);
        MPI_Count a = cbs[0];   /* count */
        MPI_Count b = cbs[1];   /* blocklength */
        assert(cbs[1]==cbs[2]); /* blocklength==stride */

        *count = a*b;
        *basetype = vbasetype[0];
        return MPI_SUCCESS;
    }
#else
    assert(combiner==MPI_COMBINER_STRUCT);
#endif
    assert(nint==3);
    assert(nadd==2);
    assert(ndts==2);

    int cnbls[3]; /* {count, blocklengths[]} */
    MPI_Aint displacements[2]; /* {0,remdisp} */
    MPI_Datatype types[2]; /* {chunks,remainder} */;
    MPI_Type_get_contents(intype, 3, 2, 2, cnbls, displacements, types);
    assert(cnbls[0]==2);
    assert(cnbls[1]==1);
    assert(cnbls[2]==1);
    assert(displacements[0]==0);

    /* Step 2: Decode the type_vector call. */

    MPI_Type_get_envelope(types[0], &nint, &nadd, &ndts, &combiner);
    assert(combiner==MPI_COMBINER_VECTOR);
    assert(nint==3);
    assert(nadd==0);
    assert(ndts==1);

    int cbs[3]; /* {count,blocklength,stride} */
    MPI_Datatype vbasetype[1];
    MPI_Type_get_contents(types[0], 3, 0, 1, cbs, NULL, vbasetype);
    assert(/* blocklength = */ cbs[1]==bigmpi_int_max);
    assert(/* stride = */ cbs[2]==bigmpi_int_max);

    /* chunk count - see above */
    MPI_Count c = cbs[0];

    /* Step 3: Decode the type_contiguous call. */

    MPI_Type_get_envelope(types[1], &nint, &nadd, &ndts, &combiner);
    assert(combiner==MPI_COMBINER_CONTIGUOUS);
    assert(nint==1);
    assert(nadd==0);
    assert(ndts==1);

    int ccc[1]; /* {count} */
    MPI_Datatype cbasetype[1];
    MPI_Type_get_contents(types[1], 1, 0, 1, ccc, NULL, cbasetype);

    /* remainder - see above */
    MPI_Count r = ccc[0];

    /* The underlying type of the vector and contig types must match. */
    assert(cbasetype[0]==vbasetype[0]);
    *basetype = cbasetype[0];

    /* This should not overflow because everything is already MPI_Count type. */
    *count = c*bigmpi_int_max+r;

    return MPI_SUCCESS;
}
