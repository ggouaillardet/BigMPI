#include "bigmpi_impl.h"

/* The displacements vector cannot be represented in the existing set of MPI-3
   functions because it is an integer rather than an MPI_Aint. */

typedef enum { GATHERV, SCATTERV, ALLGATHERV, ALLTOALLV, ALLTOALLW } collective_t;
typedef enum { NEIGHBORHOOD_ALLTOALLW, NONBLOCKING_BCAST, P2P, RMA } method_t;

int BigMPI_Collective(collective_t coll, method_t method,
                      const void *sendbuf,
                      MPI_Count sendcount, MPI_Count sendcounts[],
                      MPI_Aint senddispls[],
                      MPI_Datatype sendtype, MPI_Datatype sendtypes[],
                      void *recvbuf,
                      const MPI_Count recvcount, const MPI_Count recvcounts[],
                      const MPI_Aint recvdispls[],
                      MPI_Datatype recvtype, MPI_Datatype recvtypes[],
                      int root,
                      MPI_Comm comm)
{
    int rc = MPI_SUCCESS;

    int is_intercomm;
    MPI_Comm_test_inter(comm, &is_intercomm);
    if (is_intercomm)
        BigMPI_Error("BigMPI does not support intercommunicators yet.\n");

    if (sendbuf==MPI_IN_PLACE)
        BigMPI_Error("BigMPI does not support in-place in the v-collectives.  Sorry. \n");

    int size, rank;
    MPI_Comm_size(comm, &size);
    MPI_Comm_rank(comm, &rank);

    if (method==NEIGHBORHOOD_ALLTOALLW) {

        int          * newsendcounts = malloc(size*sizeof(int));          assert(newsendcounts!=NULL);
        MPI_Datatype * newsendtypes  = malloc(size*sizeof(MPI_Datatype)); assert(newsendtypes!=NULL);
        MPI_Aint     * newsdispls    = malloc(size*sizeof(MPI_Aint));     assert(newsdispls!=NULL);

        int          * newrecvcounts = malloc(size*sizeof(int));          assert(newrecvcounts!=NULL);
        MPI_Datatype * newrecvtypes  = malloc(size*sizeof(MPI_Datatype)); assert(newrecvtypes!=NULL);
        MPI_Aint     * newrdispls    = malloc(size*sizeof(MPI_Aint));     assert(newrdispls!=NULL);

        if (coll==ALLTOALLW) {
            assert(root == -1);
            BigMPI_Convert_vectors(size,
                                   0 /* splat count */, 0, sendcounts,
                                   0 /* splat type */, 0, sendtypes,
                                   0 /* zero displs */, senddispls,
                                   newsendcounts, newsendtypes, newsdispls);
            BigMPI_Convert_vectors(size,
                                   0 /* splat count */, 0, recvcounts,
                                   0 /* splat type */, 0, recvtypes,
                                   0 /* zero displs */, recvdispls,
                                   newrecvcounts, newrecvtypes, newrdispls);
        } else if (coll==ALLTOALLV) {
            assert(root == -1);
            BigMPI_Convert_vectors(size,
                                   0 /* splat count */, 0, sendcounts,
                                   1 /* splat type */, sendtype, NULL,
                                   0 /* zero displs */, senddispls,
                                   newsendcounts, newsendtypes, newsdispls);
            BigMPI_Convert_vectors(size,
                                   0 /* splat count */, 0, recvcounts,
                                   1 /* splat type */, recvtype, NULL,
                                   0 /* zero displs */, recvdispls,
                                   newrecvcounts, newrecvtypes, newrdispls);
        } else if (coll==ALLGATHERV) {
            assert(root == -1);
            BigMPI_Convert_vectors(size,
                                   1 /* splat count */, sendcount, NULL,
                                   1 /* splat type */, sendtype, NULL,
                                   1 /* zero displs */, NULL,
                                   newsendcounts, newsendtypes, newsdispls);
            BigMPI_Convert_vectors(size,
                                   0 /* splat count */, 0, recvcounts,
                                   1 /* splat type */, recvtype, NULL,
                                   0 /* zero displs */, recvdispls,
                                   newrecvcounts, newrecvtypes, newrdispls);
        } else if (coll==GATHERV) {
            assert(root != -1);
            BigMPI_Convert_vectors(size,
                                   1 /* splat count */, sendcount, NULL,
                                   1 /* splat type */, sendtype, NULL,
                                   1 /* zero displs */, NULL,
                                   newsendcounts, newsendtypes, newsdispls);
            /* Gatherv: Only the root receives data. */
            if (rank==root) {
                BigMPI_Convert_vectors(size,
                                       0 /* splat count */, 0, recvcounts,
                                       1 /* splat type */, recvtype, NULL,
                                       0 /* zero displs */, recvdispls,
                                       newrecvcounts, newrecvtypes, newrdispls);
            } else {
                BigMPI_Convert_vectors(size,
                                       1 /* splat count */, 0, NULL,
                                       1 /* splat type */, MPI_DATATYPE_NULL, NULL,
                                       1 /* zero displs */, NULL,
                                       newrecvcounts, newrecvtypes, newrdispls);
            }
        } else if (coll==SCATTERV) {
            assert(root != -1);
            /* Scatterv: Only the root sends data. */
            if (rank==root) {
                BigMPI_Convert_vectors(size,
                                       0 /* splat count */, 0, sendcounts,
                                       1 /* splat type */, sendtype, NULL,
                                       0 /* zero displs */, senddispls,
                                       newsendcounts, newsendtypes, newsdispls);
            } else {
                BigMPI_Convert_vectors(size,
                                       1 /* splat count */, 0, NULL,
                                       1 /* splat type */, MPI_DATATYPE_NULL, NULL,
                                       1 /* zero displs */, NULL,
                                       newsendcounts, newsendtypes, newsdispls);
            }
            BigMPI_Convert_vectors(size,
                                   1 /* splat count */, recvcount, NULL,
                                   1 /* splat type */, recvtype, NULL,
                                   1 /* zero displs */, NULL,
                                   newrecvcounts, newrecvtypes, newrdispls);
        } else {
            BigMPI_Error("Invalid collective chosen. \n");
        }

        MPI_Comm comm_dist_graph;
        BigMPI_Create_graph_comm(comm, root, &comm_dist_graph);
        rc = MPI_Neighbor_alltoallw(sendbuf, newsendcounts, newsdispls, newsendtypes,
                                    recvbuf, newrecvcounts, newrdispls, newrecvtypes, comm_dist_graph);
        MPI_Comm_free(&comm_dist_graph);

        for (int i=0; i<size; i++) {
            MPI_Type_free(&newsendtypes[i]);
            MPI_Type_free(&newrecvtypes[i]);
        }
        free(newsendcounts);
        free(newsdispls);
        free(newsendtypes);

        free(newrecvcounts);
        free(newrecvtypes);
        free(newrdispls);

    } else if (method==P2P) {

        /* There is no easy way to implement large-count using MPI_Alltoallw because displs is an int. */
        MPI_Request * reqs = malloc(2*size*sizeof(MPI_Request)); assert(reqs!=NULL);
        for (int i=0; i<size; i++) {
            /* No extent calculation because alltoallw does not use that. */
            MPIX_Irecv_x(recvbuf+recvdispls[i], recvcounts[i], recvtypes[i], i, i /* tag */, comm, &reqs[i]);
            MPIX_Isend_x(sendbuf+senddispls[i], sendcounts[i], sendtypes[i], i /* source */, i /* tag */, comm, &reqs[size+i]);
        }
        MPI_Waitall(2*size, reqs, MPI_STATUSES_IGNORE);
        free(reqs);

    } else if (method==RMA) {

        /* In the RMA implementation, we will treat send as source (buf) and recv as target (win). */
        MPI_Win win;
        /* This is the most (?) conservative approach possible, and assumes that datatypes are
         * noncontiguous and potentially out-of-order. */
        MPI_Aint max_size = 0;
        for (int i=0; i<size; i++) {
            MPI_Aint lb /* unused */, extent;
            MPI_Type_get_extent(recvtypes[i], &lb, &extent);
            MPI_Aint offset = recvdispls[i]+recvcounts[i]*extent;
            max_size = (offset > max_size ? offset : max_size);
        }
        MPI_Win_create(recvbuf, max_size, 1, MPI_INFO_NULL, comm, &win);
        MPI_Win_fence(0, win);
        for (int i=0; i<size; i++) {
            MPI_Put(sendbuf+senddispls[i], sendcounts[i], sendtypes[i],
                    i, recvdispls[i], recvtypes[i], recvtypes[i], win);
        }
        MPI_Win_fence(0, win);
        MPI_Win_free(&win);

    } else {
        /* This should be unreachable... */
        BigMPI_Error("Invalid method for v-collectives chosen. \n");
    }
    return rc;
}

int MPIX_Gatherv_x(const void *sendbuf, MPI_Count sendcount, MPI_Datatype sendtype,
                   void *recvbuf, const MPI_Count recvcounts[], const MPI_Aint rdispls[], MPI_Datatype recvtype,
                   int root, MPI_Comm comm)
{
    return MPI_SUCCESS;
}

int MPIX_Allgatherv_x(const void *sendbuf, MPI_Count sendcount, MPI_Datatype sendtype,
                      void *recvbuf, const MPI_Count recvcounts[], const MPI_Aint rdispls[], MPI_Datatype recvtype,
                      MPI_Comm comm)
{
    return MPI_SUCCESS;
}

int MPIX_Scatterv_x(const void *sendbuf, const MPI_Count sendcounts[], const MPI_Aint sdispls[], MPI_Datatype sendtype,
                    void *recvbuf, MPI_Count recvcount, MPI_Datatype recvtype, int root, MPI_Comm comm)
{
    return MPI_SUCCESS;
}

int MPIX_Alltoallv_x(const void *sendbuf, const MPI_Count sendcounts[], const MPI_Aint sdispls[], MPI_Datatype sendtype,
                     void *recvbuf, const MPI_Count recvcounts[], const MPI_Aint rdispls[], MPI_Datatype recvtype,
                     MPI_Comm comm)
{
    return MPI_SUCCESS;
}

int MPIX_Alltoallw_x(const void *sendbuf, const MPI_Count sendcounts[], const MPI_Aint sdispls[], const MPI_Datatype sendtypes[],
                     void *recvbuf, const MPI_Count recvcounts[], const MPI_Aint rdispls[], const MPI_Datatype recvtypes[],
                     MPI_Comm comm)
{
    return MPI_SUCCESS;
}
