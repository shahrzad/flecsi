/*
    @@@@@@@@  @@           @@@@@@   @@@@@@@@ @@
   /@@/////  /@@          @@////@@ @@////// /@@
   /@@       /@@  @@@@@  @@    // /@@       /@@
   /@@@@@@@  /@@ @@///@@/@@       /@@@@@@@@@/@@
   /@@////   /@@/@@@@@@@/@@       ////////@@/@@
   /@@       /@@/@@//// //@@    @@       /@@/@@
   /@@       @@@//@@@@@@ //@@@@@@  @@@@@@@@ /@@
   //       ///  //////   //////  ////////  //

   Copyright (c) 2016, Los Alamos National Security, LLC
   All rights reserved.
                                                                              */
#pragma once

/*! @file */

#include <flecsi-config.h>

#if !defined(FLECSI_ENABLE_LEGION)
#error FLECSI_ENABLE_LEGION not defined! This file depends on Legion!
#endif

#include <default_mapper.h>
#include <legion.h>
#include <legion_mapping.h>
#include <mappers/default_mapper.h>

#include <flecsi/execution/context.h>
#include <flecsi/execution/legion/legion_tasks.h>

clog_register_tag(legion_mapper);

/*!
 Mapper ID

 @ingroup legion-execution
 */

enum {
  MPI_MAPPER_ID = 1,
};

namespace flecsi {
namespace execution {

/*
 The mpi_mapper_t - is a custom mapper that handles mpi-legion
 interoperability in FLeCSI

 @ingroup legion-execution
*/

class mpi_mapper_t : public Legion::Mapping::DefaultMapper
{
public:
  /*!
   Contructor. Derives from the Legion's Default Mapper

   @param machine Machine type for Legion's Realm
   @param _runtime Legion runtime
   @param local processor type: currently supports only
           LOC_PROC and TOC_PROC
   */

  mpi_mapper_t(Legion::Machine machine,
    Legion::Runtime * _runtime,
    Legion::Processor local)
    : Legion::Mapping::DefaultMapper(_runtime->get_mapper_runtime(),
        machine,
        local,
        "default"),
      machine(machine) {
    using legion_machine = Legion::Machine;
    using legion_proc = Legion::Processor;

    legion_machine::ProcessorQuery pq =
      legion_machine::ProcessorQuery(machine).same_address_space_as(local);
    for(legion_machine::ProcessorQuery::iterator pqi = pq.begin();
        pqi != pq.end(); ++pqi) {
      legion_proc p = *pqi;
      if(p.kind() == legion_proc::LOC_PROC)
        local_cpus.push_back(p);
      else if(p.kind() == legion_proc::TOC_PROC)
        local_gpus.push_back(p);
      else
        continue;

      std::map<Realm::Memory::Kind, Realm::Memory> & mem_map = proc_mem_map[p];

      legion_machine::MemoryQuery mq =
        legion_machine::MemoryQuery(machine).has_affinity_to(p);
      for(legion_machine::MemoryQuery::iterator mqi = mq.begin();
          mqi != mq.end(); ++mqi) {
        Realm::Memory m = *mqi;
        mem_map[m.kind()] = m;

        if(m.kind() == Realm::Memory::SYSTEM_MEM)
          local_sysmem = m;
      } // end for
    } // end for

    {
      clog_tag_guard(legion_mapper);
      clog(info) << "Mapper constuctor: local=" << local
                 << " cpus=" << local_cpus.size()
                 << " gpus=" << local_gpus.size() << " sysmem=" << local_sysmem
                 << std::endl;
    }
  } // end mpi_mapper_t

  /*!
    Destructor
   */
  virtual ~mpi_mapper_t(){};

  Legion::LayoutConstraintID default_policy_select_layout_constraints(
    Legion::Mapping::MapperContext ctx,
    Realm::Memory target_memory,
    const Legion::RegionRequirement & req,
    Legion::Mapping::DefaultMapper::MappingKind mapping_kind,
    bool needs_field_constraint_check,
    bool & force_new_instances) {
    // We always set force_new_instances to false since we are
    // deciding to optimize for minimizing memory usage instead
    // of avoiding Write-After-Read (WAR) dependences
    force_new_instances = false;
    std::vector<Legion::DimensionKind> ordering;
    ordering.push_back(Legion::DimensionKind::DIM_Y);
    ordering.push_back(Legion::DimensionKind::DIM_X);
    ordering.push_back(Legion::DimensionKind::DIM_F); // SOA
    Legion::OrderingConstraint ordering_constraint(
      ordering, true /*contiguous*/);
    Legion::LayoutConstraintSet layout_constraint;
    layout_constraint.add_constraint(ordering_constraint);

    // Do the registration
    Legion::LayoutConstraintID result =
      runtime->register_layout(ctx, layout_constraint);
    return result;
  }

  /*!
   Specialization of the default_policy_select_instance_region methid for FleCSI

   @param ctx Mapper Context
   @param target_memory target memory for the instance to be allocated
   @param req Reqion requirement for witch instance is going to be allocated
   @layout_constraints Layout constraints
  */
  virtual Legion::LogicalRegion default_policy_select_instance_region(
    Legion::Mapping::MapperContext ctx,
    Realm::Memory target_memory,
    const Legion::RegionRequirement & req,
    const Legion::LayoutConstraintSet & layout_constraints,
    bool force_new_instances,
    bool meets_constraints) {
    // If it is not something we are making a big region for just
    // return the region that is actually needed
    Legion::LogicalRegion result = req.region;
    if(!meets_constraints || (req.privilege == REDUCE))
      return result;

    return result;
  } // default_policy_select_instance_region

  /*!
   THis function will find a CPU variat for the task
  */
  Legion::VariantID find_cpu_variant(const Legion::Mapping::MapperContext ctx,
    Legion::TaskID task_id) {
    std::map<Legion::TaskID, Legion::VariantID>::const_iterator finder =
      cpu_variants.find(task_id);
    if(finder != cpu_variants.end())
      return finder->second;
    std::vector<Legion::VariantID> variants;
    runtime->find_valid_variants(
      ctx, task_id, variants, Legion::Processor::LOC_PROC);
    assert(variants.size() == 1); // should be exactly one for pennant
    cpu_variants[task_id] = variants[0];
    return variants[0];
  }

  void creade_reduction_instance(const Legion::Mapping::MapperContext ctx,
    const Legion::Task & task,
    Legion::Mapping::Mapper::MapTaskOutput & output,
    const Legion::Memory & target_mem,
    const size_t & indx) {
    // using dummy constraints for REDUCTION
    std::set<Legion::FieldID> dummy_fields;
    Legion::TaskLayoutConstraintSet dummy_constraints;

    size_t instance_size = 0;
    clog_assert(
      default_create_custom_instances(ctx, task.target_proc, target_mem,
        task.regions[indx], indx, dummy_fields, dummy_constraints,
        false /*need check*/, output.chosen_instances[indx], &instance_size),
      " ERROR: FleCSI mapper failed to allocate reduction instance");

    clog(info) << "task " << task.get_task_name()
               << " allocates physical instance with size " << instance_size
               << " for the region requirement #" << indx << std::endl;

    if(instance_size > 1000000000) {
      clog(error) << "task " << task.get_task_name()
                  << " is trying to allocate physical instance with \
           the size > than 1 Gb("
                  << instance_size << " )"
                  << " for the region requirement # " << indx << std::endl;
    }//if
  }//create reduction instance

  void create_compacted_instance(const Legion::Mapping::MapperContext ctx,
    const Legion::Task & task,
    Legion::Mapping::Mapper::MapTaskOutput & output,
    const Legion::Memory & target_mem,
    const Legion::LayoutConstraintSet & layout_constraints,
    const size_t & indx) {
    using namespace Legion;
    using namespace Legion::Mapping;
    const std::pair<Legion::LogicalRegion, Legion::Memory> key(
      task.regions[indx].region, target_mem);
    std::map<std::pair<LogicalRegion, Memory>, PhysicalInstance>::const_iterator
      finder = local_instances_.find(key);
    if(finder != local_instances_.end()) {
      for(size_t j = 0; j < 3; j++) {
        output.chosen_instances[indx + j].clear();
        output.chosen_instances[indx + j].push_back(finder->second);
      } // for
      return;
    } // if instance already created in this memory

    Legion::Mapping::PhysicalInstance result;
    std::vector<Legion::LogicalRegion> regions;
    bool created;

    // creating physical instance for the compacted storaged

    clog_assert((task.regions.size() >= (indx + 2)),
      "ERROR:: wrong number of regions passed to the task wirth \
               the  tag = MAPPER_COMPACTED_STORAGE");

    clog_assert((task.regions[indx].region.exists()),
      "ERROR:: pasing not existing REGION to the mapper");

    // compacting region requirements for exclusive, shared and ghost into one
    // instance
    regions.push_back(task.regions[indx].region);
    regions.push_back(task.regions[indx + 1].region);
    regions.push_back(task.regions[indx + 2].region);

    size_t instance_size = 0;
    clog_assert(runtime->find_or_create_physical_instance(ctx, target_mem,
                  layout_constraints, regions, result, created,
                  true /*acquire*/, GC_NEVER_PRIORITY, true, &instance_size),
      "ERROR: FleCSI mapper couldn't create an instance");

    clog(info) << "task " << task.get_task_name()
               << " allocates physical instance with size " << instance_size
               << " for the region requirement #" << indx << std::endl;

    if(instance_size > 1000000000) {
      clog(error) << "task " << task.get_task_name()
                  << " is trying to allocate physical compacted instance with \
                the size > than 1 Gb("
                  << instance_size << " )"
                  << " for the region requirement # " << indx << std::endl;
    }

    for(size_t j = 0; j < 3; j++) {
      output.chosen_instances[indx + j].clear();
      output.chosen_instances[indx + j].push_back(result);
    } // for
    local_instances_[key] = result;
  } // create_compacted_instance

  void create_instance(const Legion::Mapping::MapperContext ctx,
    const Legion::Task & task,
    Legion::Mapping::Mapper::MapTaskOutput & output,
    const Legion::Memory & target_mem,
    const Legion::LayoutConstraintSet & layout_constraints,
    const size_t & indx) {
    using namespace Legion;
    using namespace Legion::Mapping;
    const std::pair<Legion::LogicalRegion, Legion::Memory> key(
      task.regions[indx].region, target_mem);
    std::map<std::pair<LogicalRegion, Memory>, PhysicalInstance>::const_iterator
      finder = local_instances_.find(key);
    if(finder != local_instances_.end()) {
      output.chosen_instances[indx].clear();
      output.chosen_instances[indx].push_back(finder->second);
      return;
    }
    Legion::Mapping::PhysicalInstance result;
    std::vector<Legion::LogicalRegion> regions;
    bool created;

    regions.push_back(task.regions[indx].region);

    size_t instance_size = 0;
    clog_assert(runtime->find_or_create_physical_instance(ctx, target_mem,
                  layout_constraints, regions, result, created,
                  true /*acquire*/, GC_NEVER_PRIORITY, true, &instance_size),
      "FLeCSI mapper failed to allocate instance");

    clog(info) << "task " << task.get_task_name()
               << " allocates physical instance with size " << instance_size
               << " for the region requirement #" << indx << std::endl;

    if(instance_size > 1000000000) {
      clog(error)
        << "task " << task.get_task_name()
        << " is trying to allocate physical instance with the size > than 1 Gb("
        << instance_size << " )"
        << " for the region requirement # " << indx << std::endl;
    }//if 

    output.chosen_instances[indx].push_back(result);
    local_instances_[key] = result;
  } // create_instance

  /*!
   Specialization of the map_task funtion for FLeCSI
   By default, map_task will execute Legions map_task from DefaultMapper.
   In the case the launcher has been tagged with the
   "MAPPER_COMPACTED_STORAGE" tag, mapper will create single physical
   instance for exclusive, shared and ghost partitions for each data handle

    @param ctx Mapper Context
    @param task Legion's task
    @param input Input information about task mapping
    @param output Output information about task mapping
   */

  virtual void map_task(const Legion::Mapping::MapperContext ctx,
    const Legion::Task & task,
    const Legion::Mapping::Mapper::MapTaskInput & input,
    Legion::Mapping::Mapper::MapTaskOutput & output) {

    using namespace Legion;
    using namespace Legion::Mapping;

    output.chosen_variant = find_cpu_variant(ctx, task.task_id);
    output.target_procs = local_cpus;
    output.chosen_instances.resize(task.regions.size());

    if(task.regions.size() > 0) {

      Legion::Memory target_mem =
        DefaultMapper::default_policy_select_target_memory(
          ctx, task.target_proc, task.regions[0]);

      // creating ordering constraint (SOA )
      std::vector<Legion::DimensionKind> ordering;
      ordering.push_back(Legion::DimensionKind::DIM_Y);
      ordering.push_back(Legion::DimensionKind::DIM_X);
      ordering.push_back(Legion::DimensionKind::DIM_F); // SOA
      Legion::OrderingConstraint ordering_constraint(
        ordering, true /*contiguous*/);

      for(size_t indx = 0; indx < task.regions.size(); indx++) {

        // Filling out "layout_constraints" with the defaults
        Legion::LayoutConstraintSet layout_constraints;
        // No specialization
        layout_constraints.add_constraint(Legion::SpecializedConstraint());
        layout_constraints.add_constraint(ordering_constraint);
        // Constrained for the target memory kind
        layout_constraints.add_constraint(
          Legion::MemoryConstraint(target_mem.kind()));
        // Have all the field for the instance available
        std::vector<Legion::FieldID> all_fields;
        for(auto fid : task.regions[indx].privilege_fields) {
          all_fields.push_back(fid);
        } // for
        layout_constraints.add_constraint(
          Legion::FieldConstraint(all_fields, true));

        Legion::Mapping::PhysicalInstance result;
        std::vector<Legion::LogicalRegion> regions;
        bool created;

        // creating physical instance for the reduction task
        if(task.regions[indx].privilege == REDUCE) {
          creade_reduction_instance(ctx, task, output, target_mem, indx);
        }
        else if(task.regions[indx].tag == EXCLUSIVE_LR) {

          create_compacted_instance(
            ctx, task, output, target_mem, layout_constraints, indx);
          indx = indx + 2;
        }
        else {
          create_instance(
            ctx, task, output, target_mem, layout_constraints, indx);
        } // end if
      } // end for

    } // end if

    runtime->acquire_instances(ctx, output.chosen_instances);

  } // map_task

  virtual void slice_task(const Legion::Mapping::MapperContext ctx,
    const Legion::Task & task,
    const Legion::Mapping::Mapper::SliceTaskInput & input,
    Legion::Mapping::Mapper::SliceTaskOutput & output) {
    using legion_proc = Legion::Processor;
    context_t & context_ = context_t::instance();

    if(task.tag == MAPPER_SUBRANK_LAUNCH) {
      // expect a 1-D index domain
      assert(input.domain.get_dim() == 1);
      // send the whole domain to our local processor
      output.slices.resize(1);
      output.slices[0].domain = input.domain;
      output.slices[0].proc = task.target_proc;
      return;
    } // end if MAPPER_SUBRANK_LAUNCH

    if((task.tag == MAPPER_FORCE_RANK_MATCH) ||
       (task.tag == MAPPER_COMPACTED_STORAGE)) {
      // expect a 1-D index domain - each point goes to the corresponding node
      assert(input.domain.get_dim() == 1);
      LegionRuntime::Arrays::Rect<1> r = input.domain.get_rect<1>();

      // go through all the CPU processors and find a representative for each
      //  node (i.e. address space)
      std::map<int, Legion::Processor> targets;

      Legion::Machine::ProcessorQuery pq =
        Legion::Machine::ProcessorQuery(machine).only_kind(
          Legion::Processor::LOC_PROC);
      for(Legion::Machine::ProcessorQuery::iterator it = pq.begin();
          it != pq.end(); ++it) {
        Legion::Processor p = *it;
        int a = p.address_space();
        if(targets.count(a) == 0)
          targets[a] = p;
      }

      output.slices.resize(1);
      for(int a = r.lo[0]; a <= r.hi[0]; a++) {
        assert(targets.count(a) > 0);
        output.slices[0].domain = // Legion::Domain::from_rect<1>(
          Legion::Rect<1>(a, a);
        output.slices[0].proc = targets[a];
      }
      return;
    } // MAPPER_FORCE_RANK_MATCH

    DefaultMapper::slice_task(ctx, task, input, output);
    // end else
  }

private:
  std::map<Legion::Processor, std::map<Realm::Memory::Kind, Realm::Memory>>
    proc_mem_map;
  Realm::Memory local_sysmem;
  Realm::Machine machine;

  // the map of the locac intances that have been already created
  // the first key is the pair of Logical region and Memory that is
  // used as an identifier for the instance
  std::map<std::pair<Legion::LogicalRegion, Legion::Memory>,
    Legion::Mapping::PhysicalInstance>
    local_instances_;

protected:
  std::map<Legion::TaskID, Legion::VariantID> cpu_variants;
};

/*!
 mapper_registration is used to replace DefaultMapper with mpi_mapper_t in
 FLeCSI

 @ingroup legion-execution
 */

inline void
mapper_registration(Legion::Machine machine,
  Legion::HighLevelRuntime * rt,
  const std::set<Legion::Processor> & local_procs) {
  for(std::set<Legion::Processor>::const_iterator it = local_procs.begin();
      it != local_procs.end(); it++) {
    mpi_mapper_t * mapper = new mpi_mapper_t(machine, rt, *it);
    rt->replace_default_mapper(mapper, *it);
  }
} // mapper registration

} // namespace execution
} // namespace flecsi
