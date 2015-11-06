#include "coreneuron/nrnconf.h"
#include "coreneuron/nrnoc/multicore.h"
#include "coreneuron/nrnoc/nrnoc_decl.h"
#include "coreneuron/nrnmpi/nrnmpi.h"
#include "coreneuron/nrniv/partrans.h"
#include <map>
#include <vector>

using namespace::nrn_partrans;

nrn_partrans::SetupInfo* nrn_partrans::setup_info_;


class SidData {
  public:
  std::vector<int> tids_;
  std::vector<int> indices_;
};

#if NRNLOGSGID
#define sgid_alltoallv nrnmpi_long_alltoallv
#else
#define sgid_alltoallv nrnmpi_int_alltoallv
#endif

#define HAVEWANT_t sgid_t
#define HAVEWANT_alltoallv sgid_alltoallv
#define HAVEWANT2Int std::map<sgid_t, int>
#include "coreneuron/nrniv/have2want.h"

void nrn_partrans::gap_mpi_setup(int ngroup) {
  printf("%d gap_mpi_setup ngroup=%d\n", nrnmpi_myid, ngroup);

  //create and fill halfgap_info using first available...
  halfgap_info = new HalfGap_Info;
  HalfGap_Info& hgi = *halfgap_info;
  for (int tid = 0; tid < ngroup; ++tid) {
    nrn_partrans::SetupInfo& si = setup_info_[tid];
    if (si.ntar) {
      hgi.ix_vpre = si.ix_vpre;
      hgi.type = si.type;
      hgi.sz = nrn_prop_param_size_[hgi.type];
      hgi.layout = nrn_mech_data_layout_[hgi.type];
    }
  }

  // count total_nsrc, total_ntar and allocate (total_ntar too large but...)
  // also allocate arrays in TransferThreadData.
  int total_nsrc=0, total_ntar=0;
  for (int tid = 0; tid < ngroup; ++tid) {
    nrn_partrans::SetupInfo& si = setup_info_[tid];
    nrn_partrans::TransferThreadData& ttd = transfer_thread_data_[tid];
    total_nsrc += si.nsrc;
    total_ntar += si.ntar;

    ttd.ntar = si.ntar; // for debugging
    ttd.insrc_indices = new int[si.ntar]; //memb_list[type].nodecount
    ttd.nsrc = si.nsrc;
    ttd.v_indices = new int[ttd.nsrc];
  }

  // have and want arrays
  sgid_t* have = new sgid_t[total_nsrc];
  sgid_t* want = new sgid_t[total_ntar]; // more than needed

  // map from sid_src to (tid, index) into v_indices
  // and sid_target to lists of (tid, index) for memb_list
  // also count the map sizes and fill have and want arrays
  std::map<sgid_t, SidData> src2data;
  std::map<sgid_t, SidData> tar2data;
  int src2data_size=0, tar2data_size=0; // number of unique sids
  for (int tid = 0; tid < ngroup; ++tid) {
    SetupInfo& si = setup_info_[tid];
    for (int i=0; i < si.nsrc; ++i) {
      sgid_t sid = si.sid_src[i];
      SidData sd;
      sd.tids_.push_back(tid);
      sd.indices_.push_back(i);
      src2data[sid] = sd;
      have[src2data_size] = sid;
      src2data_size++;
    }
    for (int i=0; i < si.ntar; ++i) {
      sgid_t sid = si.sid_target[i];
      if (tar2data.find(sid) == tar2data.end()) {
        SidData sd;
        tar2data[sid] = sd;
        want[tar2data_size] = sid;
        tar2data_size++;
      }
      SidData& sd = tar2data[sid];
      sd.tids_.push_back(tid);
      sd.indices_.push_back(i);
    }
  }
    
  // 2) Call the have_to_want function.
  sgid_t* send_to_want;
  sgid_t* recv_from_have;

  have_to_want(have, src2data_size, want, tar2data_size,
    send_to_want, outsrccnt_, outsrcdspl_,
    recv_from_have, insrccnt_, insrcdspl_,
    default_rendezvous);

  int nhost = nrnmpi_numprocs;

  // sanity check. all the sgids we are asked to send, we actually have
  for (int i=0; i < outsrcdspl_[nhost]; ++i) {
    sgid_t sgid = send_to_want[i];
    assert(src2data.find(sgid) != src2data.end());
  }

  // sanity check. all the sgids we receive, we actually need.
  for (int i=0; i < insrcdspl_[nhost]; ++i) {
    sgid_t sgid = recv_from_have[i];
    assert(tar2data.find(sgid) != tar2data.end());
  }

  // clean up a little
  delete [] have;
  delete [] want;

  insrc_buf_ = new double[insrcdspl_[nhost]];
  outsrc_buf_ = new double[outsrcdspl_[nhost]];

  // fill thread actual_v to send arrays. (offsets and layout later).
  for (int i = 0; i < outsrcdspl_[nhost]; ++i) {
    sgid_t sgid = send_to_want[i];
    SidData& sd = src2data[sgid];
    // only one item in the lists.
    int tid = sd.tids_[0];
    int index = sd.indices_[0];
    transfer_thread_data_[tid].v_indices[index] = i;
  }

  // fill thread receive to vpre arrays. (offsets and layout later).
  for (int i = 0; i < insrcdspl_[nhost]; ++i) {
    sgid_t sgid = recv_from_have[i];
    SidData& sd = tar2data[sgid];
    // there may be several items in the lists.
    for (unsigned j = 0; j < sd.tids_.size(); ++j) {
      int tid = sd.tids_[j];
      int index = sd.indices_[j];
      transfer_thread_data_[tid].insrc_indices[index] = i;
    }
  }

#if 0
  // things look ok so far?
  for (int tid=0; tid < ngroup; ++tid) {
    nrn_partrans::SetupInfo& si = setup_info_[tid];
    nrn_partrans::TransferThreadData& ttd = transfer_thread_data_[tid];
    for (int i=0; i < si.nsrc; ++i) {
      printf("%d %d src sid=%d v_index=%d\n", nrnmpi_myid, tid, si.sid_src[i], si.v_indices[i]);
    }
    for (int i=0; i < si.ntar; ++i) {
      printf("%d %d tar sid=%d i=%d\n", nrnmpi_myid, tid, si.sid_target[i], i);
    }
    for (int i=0; i < ttd.nsrc; ++i) {
      printf("%d %d src i=%d v_index=%d\n", nrnmpi_myid, tid, i, ttd.v_indices[i]);
    }
    for (int i=0; i < ttd.ntar; ++i) {
      printf("%d %d tar i=%d insrc_index=%d\n", nrnmpi_myid, tid, i, ttd.insrc_indices[i]);
    }
  }
#endif

  // cleanup
  for (int tid = 0; tid < ngroup; ++tid) {
    SetupInfo& si = setup_info_[tid];
    delete [] si.sid_src;
    delete [] si.v_indices;
    delete [] si.sid_target;
  }
  delete [] setup_info_;

}

void nrn_partrans::gap_thread_setup(NrnThread& nt) {
  //printf("%d gap_thread_setup tid=%d\n", nrnmpi_myid, nt.id);
  nrn_partrans::TransferThreadData& ttd = transfer_thread_data_[nt.id];

  ttd.halfgap_ml = nt._ml_list[halfgap_info->type];
#if 0
  int ntar = ttd.halfgap_ml->nodecount;
  assert(ntar == ttd.ntar);
  int sz =halfgap_info->sz;

  for (int i=0; i < ntar; ++i) {
    ttd.insrc_indices[i] += sz;
  }
#endif
}




