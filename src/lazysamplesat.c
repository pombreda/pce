#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "memalloc.h"
#include "prng.h"
#include "int_array_sort.h"
#include "array_hash_map.h"
#include "gcd.h"
#include "utils.h"
#include "samplesat.h"
#include "print.h"
#include "vectors.h"
#include "samplesat.h"
#ifdef MINGW

/*
 * Need some version of random()
 * rand() exists on mingw but random() does not
 */
static inline int random(void) {
	return rand();
}

#endif

int32_t pred_cardinality(pred_tbl_t *pred_tbl, sort_table_t *sort_table,
		int32_t predicate) {
	if (predicate < 0 || predicate >= pred_tbl->num_preds) {
		return -1;
	}
	int32_t card = 1;
	pred_entry_t *entry = &(pred_tbl->entries[predicate]);
	int32_t i;
	for (i = 0; i < entry->arity; i++) {
		card *= sort_table->entries[entry->signature[i]].cardinality;
	}
	return card;
}

int32_t all_atoms_cardinality(pred_tbl_t *pred_tbl, sort_table_t *sort_table) {
	int32_t i;
	int32_t card = 0;
	for (i = 1; i < pred_tbl->num_preds; i++) {
		card += pred_cardinality(pred_tbl, sort_table, i);
	}
	return card;
}

//static clause_buffer_t rule_atom_buffer = {0, NULL}; 

// void rule_atom_buffer_resize(int32_t size){
//     if (rule_atom_buffer.data == NULL){
//     rule_atom_buffer.data = (int32_t *) safe_malloc(INIT_CLAUSE_SIZE * sizeof(int32_t));
//     rule_atom_buffer.size = INIT_CLAUSE_SIZE;
//   }
//   int32_t size = rule_atom_buffer.size;
//   if (size < length){
//     if (MAXSIZE(sizeof(int32_t), 0) - size <= size/2){
//       out_of_memory();
//     }
//     size += size/2;
//     rule_atom_buffer.data =
//       (int32_t  *) safe_realloc(rule_atom_buffer.data, size * sizeof(int32_t));
//     rule_atom_buffer.size = size;
//   }
// }

// bool check_clause_instance(samp_table_t *table,
// 			   substit_buffer_t *substit,
// 			   samp_rule_t *rule,
// 			   int32_t atom_index){//index of atom being activated
//   //Use a local buffer to build the literals and check
//   //if they are active and not fixed true.
//   pred_tbl_t pred_tbl = &(table->pred_table.pred_tbl);
//   atom_table_t atom_table = &(table->atom_table);
//   int32_t predicate, arity, i, j;
//   samp_atom_t *atom;
//     samp_atom_t *rule_atom; 
//   array_hmap_pair_t *atom_map;
//   for (i = 0; i < rule->num_lits; i++){//for each literal
//     if (i != atom_index){//if literal is not the one being activated
//       atom = rule->literals[i]->atom;
//       predicate = atom->pred; 
//       arity = table->pred_table.pred_tbl[predicate].arity;
//       resize_rule_atom_buffer(arity);
//       rule_atom = (samp_atom_t *) &(rule_atom_buffer->data);
//       rule_atom->pred = predicate; 
//       for (j = 0; j < arity; j++){//copy each instantiated argument
// 	if (atom->args[j] < 0){
// 	  rule_atom->args[j] = subst_buffer->entries[-atom->args[j] - 1];
// 	} else {
// 	  rule_atom->args[j] = atom->args[j];
// 	}
//       }
//       //find the index of the atom
//       atom_map = array_size_hmap_find(&(atom_table->atom_var_hash),
// 				  arity + 1,
// 				  (int32_t *) rule_atom);
//       if (atom_map == NULL) return false;//atom is inactive
//       if (rule->neg &&
// 	  table->assignment[atom_map->val] == v_fixed_false){
// 	return false;//literal is fixed true
//       }
//       if (!rule->neg &&
// 	  table->assignment[atom_map->val] == v_fixed_true){
// 	return false;//literal is fixed true
//       }
//     }
//   }
//   return true;
// }

/**
 * choose a random atom for simulated annealing step in
 * sample SAT. The lazy version of choose_unfixed_variable
 */
int32_t choose_random_atom(samp_table_t *table) {
	uint32_t i, atom_num, anum;
	int32_t card, all_card, acard, pcard, predicate;
	pred_tbl_t *pred_tbl = &table->pred_table.pred_tbl; // Indirect preds
	atom_table_t *atom_table = &table->atom_table;
	sort_table_t *sort_table = &table->sort_table;
	pred_entry_t *pred_entry;

	// Get the number of possible indirect atoms
	all_card = all_atoms_cardinality(pred_tbl, sort_table);

	atom_num = random_uint(all_card);
	//assert(valid_table(table));

	predicate = 1; // Skip past true
	acard = 0;
	while (true) { //determine the predicate
		assert(predicate <= pred_tbl->num_preds);
		pcard = pred_cardinality(pred_tbl, sort_table, predicate);
		if (acard + pcard > atom_num) {
			break;
		}
		acard += pcard;
		predicate++;
	}
	//assert(valid_table(table));
	assert(pred_cardinality(pred_tbl, sort_table, predicate) != 0);

	anum = atom_num - acard; //gives the position of atom within predicate
	//Now calculate the arguments.  We represent the arguments in
	//little-endian form
	pred_entry = &pred_tbl->entries[predicate];
	int32_t *signature = pred_entry->signature;
	int32_t arity = pred_entry->arity;
	atom_buffer_resize(arity);
	int32_t constant;
	samp_atom_t *atom = (samp_atom_t *) atom_buffer.data;
	//Build atom from atom_num by successive division
	atom->pred = predicate;
	for (i = 0; i < arity; i++) {
		card = sort_table->entries[signature[i]].cardinality;
		constant = anum % card; //card can't be zero
		anum = anum / card;
		if (sort_table->entries[signature[i]].constants == NULL) {
			// Must be an integer
			atom->args[i] = constant;
		} else {
			atom->args[i] = sort_table->entries[signature[i]].constants[constant];
			// Quick typecheck
			assert(const_sort_index(atom->args[i], &table->const_table) == signature[i]);
		}
	}

	//assert(valid_table(table));

	array_hmap_pair_t *atom_map;
	atom_map = array_size_hmap_find(&atom_table->atom_var_hash, arity + 1,
			(int32_t *) atom);
	//assert(valid_table(table));
	if (atom_map == NULL) { //need to activate atom
		if (get_verbosity_level() >= 5) {
			printf("Activating atom (at sample %d) ", atom_table->num_samples);
			print_atom(atom, table);
			printf("\n");
		}

		activate_atom(table, atom);

		atom_map = array_size_hmap_find(&atom_table->atom_var_hash, arity + 1,
				(int32_t *) atom);
		if (atom_map == NULL) {
			printf("Something is wrong in choose_random_atom\n");
			return 0;
		} else {
			return atom_map->val;
		}
	} else {
		return atom_map->val;
	}
}

/** 
 * TODO: To debug.  A predicate has default value of true or false.  A clause
 * also has default value of true or false.
 *
 * When the default value of a clause is true: We initially calculate the
 * M-membership for active clauses only.  If some clauses are activated later
 * during the sample SAT, they need to pass the M-membership test to be put
 * into M.
 *
 *     e.g., Fr(x,y) and Sm(x) implies Sm(y) i.e., ~Fr(x,y) or ~Sm(x) or Sm(y)
 *
 *     If Sm(A) is activated (as true) at some stage, do we need to activate
 *     all the clauses related to it? No. We consider two cases:
 *
 *     If y\A, nothing changed, nothing will be activated.
 *
 *     If x\A, if there is some B, Fr(A,B) is also true, and Sm(B) is false
 *     (inactive or active but with false value), [x\A,y\B] will be activated.
 *
 *     How do we do it? We index the active atoms by each argument. e.g.,
 *     Fr(A,?) or Fr(?,B). If a literal is activated to a satisfied value,
 *     e.g., the y\A case above, do nothing. If a literal is activated to an
 *     unsatisfied value, e.g., the x\A case, we check the active atoms of
 *     Fr(x,y) indexed by Fr(A,y). Since Fr(x,y) has a default value of false
 *     that makes the literal satisfied, only the active atoms of Fr(x,y) may
 *     relate to a unsat clause.  Then we get only a small subset of
 *     substitution of y, which can be used to verify the last literal (Sm(y))
 *     easily.  (See the implementation details section in Poon and Domingos
 *     08)
 *
 * When the default value of a clause is false: Poon and Domingos 08 didn't
 * consider this case. In general, this case can't be solved lazily, because we
 * have to try to satisfy all the ground clauses within the sample SAT. This is
 * a rare case in real applications, cause they are usually sparse.
 *
 * What would be the case when we consider clauses with negative weight?  Or
 * more general, any formulas? First of all, MCSAT works for all FOL formulas
 * as well. So if a input formula has a negative weight, we could nagate the
 * formula and the weight. A Markov logic contains only clauses merely makes
 * the function activation of lazy MC-SAT easier.
 */
//int32_t lazy_sample_sat_body(samp_table_t *table, double sa_probability,
//		double samp_temperature, double rvar_probability) {
//	clause_table_t *clause_table = &table->clause_table;
//	atom_table_t *atom_table = &table->atom_table;
//	samp_truth_value_t *assignment;
//	int32_t dcost;
//	double choice;
//	int32_t var;
//	uint32_t clause_position;
//	samp_clause_t *link;
//	int32_t conflict = 0;
//
//	//Assumed that table is in a valid state with a random assignment.
//	//We first decide on simulated annealing vs. walksat.
//	assignment = atom_table->assignment[atom_table->current_assignment];
//	//assert(valid_table(table));
//	choice = choose();
//	if (clause_table->num_unsat_clauses <= 0 || choice < sa_probability) {
//		/*
//		 * Simulated annealing step
//		 */
//		//printf("Doing simulated annealing step, num_unsat_clauses = %d\n",
//		//       clause_table->num_unsat_clauses);
//
//		// choose a random atom
//		//assert(valid_table(table));
//		var = choose_random_atom(table);
//		//var = choose_unfixed_variable(assignment, atom_table->num_vars,
//		//			  atom_table->num_unfixed_vars);
//		//assert(valid_table(table));
//
//		if (var == -1 || fixed_tval(assignment[var])) {
//			return 0;
//		}
//
//		/**
//		 * TODO: for the lazy version, we should active the variable
//		 * and related clauses. Is it okay to use the same function
//		 * as non-lazy version?
//		 */
//		cost_flip_unfixed_variable(table, &dcost, var);
//		//assert(valid_table(table));
//		if (dcost <= 0) {
//			conflict = flip_unfixed_variable(table, var);
//			//assert(valid_table(table));
//		} else {
//			choice = choose();
//			if (choice < exp(-dcost / samp_temperature)) {
//				conflict = flip_unfixed_variable(table, var);
//				//assert(valid_table(table));
//			}
//		}
//	} else {
//		/*
//		 * Walksat step
//		 */
//		//choose an unsat clause
//		clause_position = random_uint(clause_table->num_unsat_clauses);
//		link = clause_table->unsat_clauses;
//		while (clause_position != 0) {
//			link = link->link;
//			clause_position--;
//		}
//		//assert(valid_table(table));
//		//link points to chosen clause
//		var = choose_clause_var(table, link, assignment, rvar_probability);
//		conflict = flip_unfixed_variable(table, var);
//		//assert(valid_table(table));
//	}
//	return conflict;
//}

/**
 * TODO: To be replaced by a lazy SAT solver. Not necessarily
 * need to be uniformly drawn.
 */
//int32_t first_lazy_sample_sat(samp_table_t *table, double sa_probability,
//		double samp_temperature, double rvar_probability, uint32_t max_flips) {
//	int32_t conflict;
//
//	conflict = init_sample_sat(table);
//	if (conflict == -1)
//		return -1;
//	uint32_t num_flips = max_flips;
//	while (table->clause_table.num_unsat_clauses > 0 && num_flips > 0) {
//		lazy_sample_sat_body(table, sa_probability, samp_temperature,
//				rvar_probability);
//		num_flips--;
//	}
//	if (table->clause_table.num_unsat_clauses > 0) {
//		mcsat_err("Initialization failed to find a model; increase max_flips\n");
//		return -1;
//	}
//	update_pmodel(table);
//	return 0;
//}

/*
 * One round of the mc_sat loop:
 * - select a set of live clauses from the current assignment
 * - compute a new assignment by sample_sat
 */
//void lazy_sample_sat(samp_table_t *table, double sa_probability,
//		double samp_temperature, double rvar_probability, uint32_t max_flips,
//		uint32_t max_extra_flips, bool update_counts) {
//	clause_table_t *clause_table = &table->clause_table;
//	int32_t conflict;
//
//	//assert(valid_table(table));
//	conflict = reset_sample_sat(table);
//	//assert(valid_table(table));
//
//	uint32_t num_flips = max_flips;
//	while (num_flips > 0 && conflict == 0) {
//		if (clause_table->num_unsat_clauses == 0) {
//			if (max_extra_flips <= 0) {
//				break;
//			} else {
//				max_extra_flips--;
//			}
//		}
//		conflict = lazy_sample_sat_body(table, sa_probability, samp_temperature,
//				rvar_probability);
//		//assert(valid_table(table));
//		num_flips--;
//	}
//
//	if (conflict != -1 && table->clause_table.num_unsat_clauses == 0) {
//		if (update_counts) {
//			update_pmodel(table);
//		}
//	} else {
//		/*
//		 * Sample sat did not find a model (within max_flips)
//		 * restore the earlier assignment
//		 */
//		if (conflict == -1) {
//			cprintf(2, "Hit a conflict.\n");
//		} else {
//			cprintf(2, "Failed to find a model.\n");
//		}
//
//		// Flip current_assignment (restore the saved assignment)
//		assert(table->atom_table.current_assignment == 0
//				|| table->atom_table.current_assignment == 1);
//		table->atom_table.current_assignment ^= 1;
//
//		empty_clause_lists(table);
//		init_clause_lists(&table->clause_table);
//		if (update_counts) {
//			update_pmodel(table);
//		}
//	}
//}

/*
 * Top-level LAZY-MCSAT call
 *
 * Parameters for lazy sample sat:
 * - sa_probability = probability of a simulated annealing step
 * - samp_temperature = temperature for simulated annealing
 * - rvar_probability = probability used by a Walksat step:
 *   a Walksat step selects an unsat clause and flips one of its variables
 *   - with probability rvar_probability, that variable is chosen randomly
 *   - with probability 1(-rvar_probability), that variable is the one that
 *     results in minimal increase of the number of unsat clauses.
 * - max_flips = bound on the number of sample_sat steps
 * - max_extra_flips = number of additional (simulated annealing) steps to perform
 *   after a satisfying assignment is found
 *
 * Parameter for mc_sat:
 * - max_samples = number of samples generated
 * - timeout = maximum running time for MCSAT
 * - burn_in_steps = number of burn-in steps
 * - samp_interval = sampling interval
 */
//void lazy_mc_sat(samp_table_t *table, uint32_t max_samples,
//		double sa_probability, double samp_temperature, double rvar_probability,
//		uint32_t max_flips, uint32_t max_extra_flips, uint32_t timeout,
//		uint32_t burn_in_steps, uint32_t samp_interval) {
//	int32_t conflict;
//	uint32_t i;
//	time_t fintime = 0;
//
//	conflict = first_lazy_sample_sat(table, sa_probability, samp_temperature,
//			rvar_probability, max_flips);
//	if (conflict == -1) {
//		mcsat_err("Found conflict in initialization.\n");
//		return;
//	}
//
//	//  print_state(table, 0);
//	//assert(valid_table(table));
//	if (timeout != 0) {
//		fintime = time(NULL) + timeout;
//	}
//	for (i = 0; i < burn_in_steps + max_samples * samp_interval; i++) {
//		if (i >= burn_in_steps && i % samp_interval == 0) {
//			cprintf(1, "---- sample[%"PRIu32"] ---\n",
//					(i - burn_in_steps) / samp_interval);
//			lazy_sample_sat(table, sa_probability, samp_temperature,
//					rvar_probability, max_flips, max_extra_flips, true);
//		} else {
//			lazy_sample_sat(table, sa_probability, samp_temperature,
//					rvar_probability, max_flips, max_extra_flips, false);
//		}
//		//    print_state(table, i+1);
//		//assert(valid_table(table));
//		if (timeout != 0 && time(NULL) >= fintime) {
//			break;
//		}
//	}
//
//	//print_atoms(table);
//}
