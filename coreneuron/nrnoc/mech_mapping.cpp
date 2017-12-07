#include "coreneuron/nrnoc/mech_mapping.hpp"
#include "coreneuron/utils/data_layout.hpp"
#include "coreneuron/nrnoc/nrnoc_ml.h"                    // for Memb_list definition
#include <iostream>
#include <cstring>
#include <map>

/*
 * an Offset in data array to lovate a specific value
 */
typedef size_t        Offset;
typedef int           MechId;
typedef const char*   VariableName;

struct cmp_str {
    bool operator()(char const* a, char const* b) const {
        return std::strcmp(a, b) < 0;
    }
};

/*
 * Structure that map variable names of mechanisms to their value's location (offset) in memory
 */
typedef std::map<MechId, std::map<VariableName, Offset, cmp_str> > MechNamesMapping;
static MechNamesMapping mechNamesMapping;

static void set_an_offset (int mech_id, const char* variable_name, int offset) {
  mechNamesMapping[mech_id][variable_name] = offset;
}
extern "C" double* get_var_location_from_var_name(int mech_id, const char* variable_name, Memb_list* ml, int node_index) {
    if (mechNamesMapping.find(mech_id) == mechNamesMapping.end()) {
      std::cerr << "ERROR [" << __FUNCTION__ << "] : no variable name mapping exist for mechanism id: " << mech_id << std::endl;
      return NULL;
    }
    if (mechNamesMapping.at(mech_id).find(variable_name) == mechNamesMapping.at(mech_id).end()) {
      std::cerr << "ERROR [" << __FUNCTION__ << "] : no value associtated to variable name: " << variable_name << std::endl;
      return NULL;
    }
    int variable_rank = mechNamesMapping.at(mech_id).at(variable_name); // at function can raise an out of range exeption
    int ix = getDataIndex(node_index, variable_rank, mech_id, ml);
    return &(ml->data[ix]);
}

extern "C" void register_all_variables_offsets(int mech_id, SerializedNames variable_names) {
    int idx = 0;  // first of variable_names and offsets are other informations
    int nb_parsed_variables = 0;
    int current_categorie = 1;
    int offset = -1;
    while (current_categorie < NB_MECH_VAR_CATEGORIES) {
        if (variable_names[idx]) {
            set_an_offset(mech_id, variable_names[idx], nb_parsed_variables);
            nb_parsed_variables++;
        } else {
            current_categorie++;
        }
        idx++;
    }
    idx++;
}
