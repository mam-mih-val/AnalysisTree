//
// Created by eugene on 24/09/2020.
//

#ifndef ANALYSISTREE_INFRA_BRANCHVIEW_HPP
#define ANALYSISTREE_INFRA_BRANCHVIEW_HPP

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Rtypes.h>
#include <TTree.h>

#include <AnalysisTree/BranchConfig.hpp>
#include <AnalysisTree/Detector.hpp>

namespace AnalysisTree {

/* fwd declatarions */
class IBranchView;
typedef std::shared_ptr<IBranchView> BranchViewPtr;

class IAction {

 public:
  virtual ~IAction() = default;
  virtual BranchViewPtr ApplyAction(const BranchViewPtr& tgt) = 0;
};

class IFieldRef {

 public:
  virtual ~IFieldRef() = default;
  virtual double GetValue(size_t i_channel) const = 0;
  virtual std::string GetFieldTypeStr() const = 0;
};

typedef std::shared_ptr<IFieldRef> FieldPtr;

namespace BranchViewAction {

template<typename Lambda>
IAction* NewDefineAction(const std::string& field_name, const std::vector<std::string>& lambda_args, Lambda&& lambda);

}

template<typename T>
using ResultsColumn = std::vector<T>;

template<typename T>
using ResultsMCols = std::map<std::string, ResultsColumn<T>>;

/**
 * @brief Interface class representing two-dimensional object
 * with index [channel_id, field_name + (type)]
 *
 * e.g:
 * +---+--------+---------+---------+---------+
 * |#ch|const   |vtx_x    |vtx_y    |vtx_z    |
 * +---+--------+---------+---------+---------+
 * |0  |2.000000|11.000000|12.000000|13.000000|
 * +---+--------+---------+---------+---------+
 */
class IBranchView {
 public:
  /**
   * @brief Returns name of the view
   * @return
   */
  std::string GetViewName() const;
  /**
   * @brief Returns list of fields for the view
   * @return
   */
  virtual std::vector<std::string> GetFields() const = 0;
  /**
   * @brief Returns number of channels for the view
   * @return
   */
  virtual size_t GetNumberOfChannels() const = 0;

  virtual FieldPtr GetFieldPtr(std::string field_name) const = 0;

  /**
   * @brief returns matrix of evaluation results
   * @return
   */
  virtual ResultsMCols<double> GetDataMatrix();

  /**
   * @brief set data source to new entry
   * @param entry
   */
  virtual void SetEntry(Long64_t entry) const = 0;
  /**
   * @brief Gets number of entries in data source
   * @return
   */
  virtual Long64_t GetEntries() const = 0;

  virtual BranchViewPtr Clone() const = 0;

  /**
   * @brief
   * @param name
   * @return
   */
  virtual bool HasField(const std::string &name);
  /**
   * @brief Requests enlisted field_names
   * @param field_names
   * @return
   */
  BranchViewPtr Select(const std::vector<std::string>& field_names) const;
  /**
   * @brief Selects single field
   * @param field_name
   * @return
   */
  BranchViewPtr Select(const std::string& field_name) const;
  BranchViewPtr operator[](const std::string& field_name) const;

  /**
   * @brief TODO
   * @param old_to_new_map
   * @return
   */
  BranchViewPtr RenameFields(const std::map<std::string, std::string>& old_to_new_map) const;
  BranchViewPtr RenameFields(std::string old_name, std::string new_name) const;

  /**
   * @brief Merges two compatible views
   *        N (v1) == N (v2)
   * @param other
   * @return
   */
  BranchViewPtr PlainMerge(const IBranchView& /*other */) const {
    throw std::runtime_error("Not implemented");
  }

  /**
   * @brief Defines new field from function evaluation
   * @tparam Function
   * @param new_field_name
   * @param arg_names
   * @param function
   * @return
   */
  template<typename Function>
  BranchViewPtr Define(std::string new_field_name, std::vector<std::string>&& arg_names, Function&& function) const {
    auto function_wrap = std::function(function);
    auto action_ptr = BranchViewAction::NewDefineAction(new_field_name, arg_names, std::move(function_wrap));
    return Apply(action_ptr);
  }

  template<typename Action>
  BranchViewPtr Apply(Action&& action) const {
    return action.ApplyAction(Clone());
  }

  BranchViewPtr Apply(IAction* action) const {
    auto apply_result = action->ApplyAction(Clone());
    delete action;
    return apply_result;
  }

  virtual void PrintEntry(std::ostream& os);
};

inline std::ostream& operator<<(std::ostream& os, IBranchView& view) {
  view.PrintEntry(os);
  return os;
}

namespace Details {

template<typename T>
struct EntityTraits {
  static constexpr bool is_channel_or_track = false;
  typedef T ChannelType;

  inline static size_t GetNChannels(const T& /* entity */) {
    return 1ul;
  }

  inline static T& GetChannel(T& entity, size_t i_channel) {
    if (i_channel == 0) {
      return entity;
    }
    throw std::out_of_range("Non-channel entity cannot have > 1 channels");
  }
};

template<typename T>
struct EntityTraits<Detector<T>> {
  static constexpr bool is_channel_or_track = true;
  typedef T ChannelType;

  inline static T& GetChannel(Detector<T>& det, size_t i_channel) {
    return det.GetChannel(i_channel);
  }

  inline static size_t GetNChannels(const Detector<T>& detector) {
    return detector.GetNumberOfChannels();
  }
};

}// namespace Details

/**
 * @brief Binding of AnalysisTree branch to BranchView
 * @tparam Entity type of the entity
 */
template<typename Entity>
class AnalysisTreeBranch : public IBranchView {
  struct DataPtrHolder {
    Entity* ptr{nullptr};

    Entity& ref() {
      return *ptr;
    }
  };

  class FieldRefImpl : public IFieldRef {
   public:
    FieldRefImpl(std::shared_ptr<DataPtrHolder> data,
                 ShortInt_t field_id,
                 Types field_type) : data_(data),
                                     field_id_(field_id),
                                     field_type_(field_type) {}
    double GetValue(size_t i_channel) const override {
      auto& channel = Details::EntityTraits<Entity>::GetChannel(data_->ref(), i_channel);
      if (field_type_ == Types::kInteger) {
        return channel.template GetField<int>(field_id_);
      } else if (field_type_ == Types::kFloat) {
        return channel.template GetField<float>(field_id_);
      } else if (field_type_ == Types::kBool) {
        return channel.template GetField<bool>(field_id_);
      }
      assert(false);
    }

    std::string GetFieldTypeStr() const override {
      if (field_type_ == Types::kInteger) {
        return "int";
      } else if (field_type_ == Types::kFloat) {
        return "float";
      } else if (field_type_ == Types::kBool) {
        return "bool";
      }
      assert(false);
    }

   private:
    std::shared_ptr<DataPtrHolder> data_;
    ShortInt_t field_id_{};
    Types field_type_{Types::kNumberOfTypes};
  };

 public:
  typedef typename Details::EntityTraits<Entity>::ChannelType ChannelType;

  BranchViewPtr Clone() const override {
    if (is_tree_input_) {
      return std::make_shared<AnalysisTreeBranch<Entity>>(config_, tree_name_, tree_dir_);
    } else {
      return std::make_shared<AnalysisTreeBranch<Entity>>(config_);
    }
  }

  size_t GetNumberOfChannels() const override {
    return Details::EntityTraits<Entity>::GetNChannels(data->ref());
  }

  std::vector<std::string> GetFields() const override {
    return field_names_;
  }

  void SetEntry(Long64_t entry) const override {
    tree_->GetEntry(entry);
  }

  Long64_t GetEntries() const override {
    return tree_->GetEntries();
  }

  explicit AnalysisTreeBranch(BranchConfig config) : config_(std::move(config)) {
    data = std::make_shared<DataPtrHolder>();
    data->ptr = new Entity;
    InitFields<int>();
    InitFields<float>();
    InitFields<bool>();
  }

  AnalysisTreeBranch(const BranchConfig& config, const std::string& tree_name, TDirectory* dir) : AnalysisTreeBranch(config) {
    is_tree_input_ = true;
    tree_name_ = tree_name;
    tree_dir_ = dir;
    InitTree();
  }

  FieldPtr GetFieldPtr(std::string field_name) const override {
    return fields_.at(field_name);
  }

 private:
  void InitTree() {
    tree_ = tree_dir_->Get<TTree>(tree_name_.c_str());
    tree_->SetBranchAddress(config_.GetName().c_str(), &(data.get()->ptr));
  }

  template<typename T>
  void InitFields() {
    auto field_map = config_.GetMap<T>();
    for (auto entry : field_map) {
      auto field_name = entry.first;
      auto column_field_id = entry.second;
      field_names_.emplace_back(field_name);
      auto column_field_type = config_.GetFieldType(field_name);
      fields_.emplace(field_name,std::make_shared<FieldRefImpl>(data, column_field_id, column_field_type));
    }
  }

  bool is_tree_input_{false};
  BranchConfig config_;
  std::string tree_name_;
  TDirectory *tree_dir_{nullptr};

  TTree* tree_{nullptr};

  std::shared_ptr<DataPtrHolder> data{};
  std::vector<std::string> field_names_;
  std::map<std::string, std::shared_ptr<FieldRefImpl>> fields_;
};

namespace BranchViewAction {

namespace Details {

template<typename T>
struct FunctionTraits : FunctionTraits<decltype(&T::operator())> {};
template<typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
  constexpr static size_t Arity = sizeof...(Args);
  typedef R ret_type;
};
template<typename R, typename C, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
  constexpr static size_t Arity = sizeof...(Args);
  typedef R ret_type;
};
template<typename R, typename C, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
  constexpr static size_t Arity = sizeof...(Args);
  typedef R ret_type;
};

template<typename T>
std::string GetTypeString() { return std::string(typeid(T).name()); }

template<>
inline std::string GetTypeString<double>() { return "double"; }

template<>
inline std::string GetTypeString<int>() { return "int"; }

template<>
inline std::string GetTypeString<float>() { return "float"; }

template<>
inline std::string GetTypeString<bool>() { return "bool"; }

std::vector<std::string>
GetMissingArgs(const std::vector<std::string>& args, const std::vector<std::string>& view_fields);

void ThrowMissingArgs(const std::vector<std::string>& missing_args);

template<typename Func>
class LambdaFieldRef : public IFieldRef {
  static constexpr size_t function_arity = Details::FunctionTraits<Func>::Arity;
  typedef typename Details::FunctionTraits<Func>::ret_type function_ret_type;

 public:
  LambdaFieldRef(Func lambda, std::vector<FieldPtr> lambda_args) : lambda_(std::move(lambda)), lambda_args_(std::move(lambda_args)) {
    if (function_arity != lambda_args_.size()) {
      throw std::out_of_range("Function arity is not consistent with number of passed arguments");
    }
  }

  double GetValue(size_t i_channel) const final {
    return GetValueImpl(i_channel, std::make_index_sequence<function_arity>());
  }

  function_ret_type GetValueRaw(size_t i_channel) const {
    return GetValueImpl(i_channel, std::make_index_sequence<function_arity>());
  }

  std::string GetFieldTypeStr() const override {
    return Details::GetTypeString<function_ret_type>();
  }

 private:
  template<size_t ArgIndex>
  double GetArgValue(size_t i_channel) const {
    return lambda_args_[ArgIndex]->GetValue(i_channel);
  }

  template<size_t... ArgIndex>
  function_ret_type
  GetValueImpl(size_t i_channel, std::index_sequence<ArgIndex...>) const {
    return lambda_(GetArgValue<ArgIndex>(i_channel)...);
  }

  Func lambda_;
  std::vector<FieldPtr> lambda_args_;
};

}// namespace Details

template<typename Function>
class BranchViewDefineAction : public IAction {

  class DefineActionResultImpl : public IBranchView {
   public:
    DefineActionResultImpl(std::string defined_field_name,
                           std::vector<std::string> lambda_args,
                           Function lambda,
                           BranchViewPtr origin) : defined_field_name_(std::move(defined_field_name)),
                                                   lambda_args_(std::move(lambda_args)),
                                                   lambda_(std::move(lambda)),
                                                   origin_(std::move(origin)) {
      std::vector<FieldPtr> lambda_args_ptrs;
      lambda_args_ptrs.reserve(lambda_args_.size());
      for (auto& arg_field_name : lambda_args_) {
        lambda_args_ptrs.emplace_back(origin_->GetFieldPtr(arg_field_name));
      }
      defined_field_ptr_ = std::make_shared<Details::LambdaFieldRef<Function>>(lambda_, lambda_args_ptrs);
    }
    Long64_t GetEntries() const override {
      return origin_->GetEntries();
    }
    std::vector<std::string> GetFields() const final {
      std::vector<std::string> fields({defined_field_name_});
      auto origin_fields = origin_->GetFields();
      std::copy(origin_fields.begin(), origin_fields.end(), std::back_inserter(fields));
      return fields;
    }
    size_t GetNumberOfChannels() const final {
      return origin_->GetNumberOfChannels();
    }
    void SetEntry(Long64_t entry) const final {
      origin_->SetEntry(entry);
    }
    BranchViewPtr Clone() const final {
      return std::make_shared<DefineActionResultImpl>(defined_field_name_, lambda_args_, lambda_, origin_->Clone());
    }
    FieldPtr GetFieldPtr(std::string field_name) const final {
      if (field_name == defined_field_name_) {
        return defined_field_ptr_;
      }
      return origin_->GetFieldPtr(field_name);
    }

    std::string defined_field_name_;
    std::vector<std::string> lambda_args_;
    Function lambda_;
    BranchViewPtr origin_;
    FieldPtr defined_field_ptr_;
  };

 public:
  BranchViewDefineAction(std::string field_name,
                         std::vector<std::string> lambda_args,
                         Function lambda) : defined_field_name_(std::move(field_name)),
                                            lambda_args_(std::move(lambda_args)),
                                            lambda_(std::move(lambda)) {
  }
  BranchViewPtr ApplyAction(const BranchViewPtr& origin) final {
    /* check if all fields exist in the origin */
    auto missing_args = Details::GetMissingArgs(lambda_args_, origin->GetFields());
    Details::ThrowMissingArgs(missing_args);
    /* check if new field is already defined in the origin */
    auto origin_fields = origin->GetFields();
    if (std::find(origin_fields.begin(), origin_fields.end(), defined_field_name_) != origin_fields.end()) {
      throw std::runtime_error("New variable already exists in the input view");
    }

    return std::make_shared<DefineActionResultImpl>(defined_field_name_, lambda_args_, lambda_, origin);
  }

 private:
  std::string defined_field_name_;
  std::vector<std::string> lambda_args_;
  Function lambda_;
};

template<typename Lambda>
IAction* NewDefineAction(const std::string& field_name, const std::vector<std::string>& lambda_args, Lambda&& lambda) {
  return new BranchViewDefineAction(field_name, lambda_args, std::function(std::forward<Lambda>(lambda)));
}

template<typename Lambda>
IAction* NewDefineAction(const std::string& field_name, std::initializer_list<std::string> lambda_args, Lambda&& lambda) {
  std::vector<std::string> lambda_args_v(lambda_args.begin(), lambda_args.end());
  return NewDefineAction(field_name, lambda_args_v, lambda);
}

template<typename Function>
class BranchViewFilterAction : public IAction {
  typedef typename Details::FunctionTraits<Function>::ret_type function_ret_type;

  class FilterActionResultImpl : public IBranchView {
    struct ChannelsHolder {
      bool is_initialized{false};
      std::vector<size_t> channels;
    };

    typedef std::shared_ptr<ChannelsHolder> ChannelsHolderPtr;

    class FieldRefImpl : public IFieldRef {
     public:
      FieldRefImpl(FieldPtr origin_field, const ChannelsHolderPtr& cache) : origin_field_(std::move(origin_field)), cache_(cache) {}
      double GetValue(size_t i_channel) const override {
        return origin_field_->GetValue(cache_->channels[i_channel]);
      }
      std::string GetFieldTypeStr() const override {
        return origin_field_->GetFieldTypeStr();
      }

     private:
      FieldPtr origin_field_;
      ChannelsHolderPtr cache_;
    };

   public:
    FilterActionResultImpl(Function lambda,
                           std::vector<std::string> lambda_args,
                           const BranchViewPtr& origin) : lambda_(lambda),
                                                          lambda_args_(std::move(lambda_args)),
                                                          origin_(origin) {
      using LambdaFieldRef = Details::LambdaFieldRef<Function>;

      std::vector<FieldPtr> lambda_args_ptrs;
      lambda_args_ptrs.reserve(lambda_args_.size());
      for (auto& arg_name : lambda_args_) {
        lambda_args_ptrs.emplace_back(origin->GetFieldPtr(arg_name));
      }
      predicate_ = std::make_shared<LambdaFieldRef>(lambda_, lambda_args_ptrs);
      cache_ = std::make_shared<ChannelsHolder>();
    }
    std::vector<std::string> GetFields() const override {
      return origin_->GetFields();
    }
    size_t GetNumberOfChannels() const override {
      return cache_->channels.size();
    }
    FieldPtr GetFieldPtr(std::string field_name) const override {
      return std::make_shared<FieldRefImpl>(origin_->GetFieldPtr(field_name), cache_);
    }
    void SetEntry(Long64_t entry) const override {
      origin_->SetEntry(entry);
      UpdateCache();
    }
    Long64_t GetEntries() const override {
      return origin_->GetEntries();
    }
    BranchViewPtr Clone() const override {
      return std::make_shared<FilterActionResultImpl>(lambda_, lambda_args_, origin_->Clone());
    }

   private:
    void UpdateCache() const {
      cache_->is_initialized = true;
      cache_->channels.clear();
      cache_->channels.reserve(origin_->GetNumberOfChannels());
      for (size_t i_channel = 0; i_channel < origin_->GetNumberOfChannels(); i_channel++) {
        auto predicate_result = predicate_->GetValueRaw(i_channel);
        if (predicate_result) {
          cache_->channels.push_back(i_channel);
        }
      }
    }

    Function lambda_;
    std::vector<std::string> lambda_args_;
    BranchViewPtr origin_;

    ChannelsHolderPtr cache_;
    std::shared_ptr<Details::LambdaFieldRef<Function>> predicate_;
  };

 public:
  BranchViewFilterAction(std::vector<std::string> lambda_args, Function lambda) : lambda_args_(std::move(lambda_args)), lambda_(std::move(lambda)) {}
  BranchViewPtr ApplyAction(const BranchViewPtr& origin) override {
    /* check if all fields exist in the origin */
    auto missing_args = Details::GetMissingArgs(lambda_args_, origin->GetFields());
    Details::ThrowMissingArgs(missing_args);

    if (!std::is_same_v<bool, function_ret_type>) {
      throw std::runtime_error("Function return type must be boolean");
    }

    return std::make_shared<FilterActionResultImpl>(lambda_, lambda_args_, origin);
  }

 private:
  std::vector<std::string> lambda_args_;
  Function lambda_;
};

template<typename Lambda>
IAction* NewFilterAction(std::initializer_list<std::string> lambda_args, Lambda&& lambda) {
  std::vector<std::string> lambda_args_v(lambda_args.begin(), lambda_args.end());
  return new BranchViewFilterAction<Lambda>(lambda_args_v, std::forward<Lambda>(lambda));
}

class SelectFieldsAction {

  class SelectFieldsActionResultImpl : public IBranchView {
   public:
    std::vector<std::string> GetFields() const override {
      return selected_fields_;
    }
    size_t GetNumberOfChannels() const override {
      return origin_->GetNumberOfChannels();
    }
    void SetEntry(Long64_t entry) const override {
      return origin_->SetEntry(entry);
    }
    Long64_t GetEntries() const override {
      return origin_->GetEntries();
    }
    BranchViewPtr Clone() const override {
      auto newView = std::make_shared<SelectFieldsActionResultImpl>();
      newView->origin_ = origin_->Clone();
      newView->selected_fields_ = selected_fields_;
      return newView;
    }
    FieldPtr GetFieldPtr(std::string field_name) const override {
      return origin_->GetFieldPtr(field_name);
    }

    BranchViewPtr origin_;
    std::vector<std::string> selected_fields_;
  };

 public:
  explicit SelectFieldsAction(std::vector<std::string> selectedFields) : selected_fields_(std::move(selectedFields)) {}
  BranchViewPtr ApplyAction(const BranchViewPtr& origin) {
    auto missing_args = Details::GetMissingArgs(selected_fields_, origin->GetFields());
    Details::ThrowMissingArgs(missing_args);

    auto action_result = std::make_shared<SelectFieldsActionResultImpl>();
    action_result->selected_fields_ = selected_fields_;
    action_result->origin_ = origin;
    return action_result;
  }

 private:
  std::vector<std::string> selected_fields_;
};

class RenameFieldsAction : public IAction {

  class RenameFieldsActionResultImpl : public IBranchView {
   public:
    RenameFieldsActionResultImpl(std::vector<std::string> new_fields,
                                 std::map<std::string, std::string> new_to_old_map,
                                 BranchViewPtr origin) : new_fields_(std::move(new_fields)),
                                                         new_to_old_map_(std::move(new_to_old_map)),
                                                         origin_(std::move(origin)) {
    }
    std::vector<std::string> GetFields() const final {
      return new_fields_;
    }
    size_t GetNumberOfChannels() const final {
      return origin_->GetNumberOfChannels();
    }
    FieldPtr GetFieldPtr(std::string field_name) const final {
      decltype(new_to_old_map_)::const_iterator find_it;
      if ((find_it = new_to_old_map_.find(field_name)) != new_to_old_map_.end()) {
        return origin_->GetFieldPtr(find_it->second);
      }
      /* not found in map */
      return origin_->GetFieldPtr(field_name);
    }
    void SetEntry(Long64_t entry) const final {
      origin_->SetEntry(entry);
    }
    Long64_t GetEntries() const final {
      return origin_->GetEntries();
    }
    BranchViewPtr Clone() const final {
      return std::make_shared<RenameFieldsActionResultImpl>(new_fields_, new_to_old_map_, origin_->Clone());
    }

   private:
    std::vector<std::string> new_fields_;
    std::map<std::string, std::string> new_to_old_map_;
    BranchViewPtr origin_;
  };

 public:
  RenameFieldsAction(const std::map<std::string, std::string>& old_to_new_map) : old_to_new_map_(old_to_new_map) {}
  BranchViewPtr ApplyAction(const BranchViewPtr& origin) override {
    std::map<std::string, std::string> new_to_old_map;
    std::vector<std::string> fields_to_rename;
    fields_to_rename.reserve(old_to_new_map_.size());
    auto origin_fields = origin->GetFields();
    std::vector<std::string> fields_after_rename(origin_fields.begin(), origin_fields.end());

    /* invert map, make sure there is no duplication in new set */
    for (auto& entry : old_to_new_map_) {
      auto old_value = entry.first;
      auto new_value = entry.second;
      fields_to_rename.emplace_back(old_value);
      auto emplace_result = new_to_old_map.emplace(new_value, old_value);
      /* check if no duplications on the right side of old_to_new map */
      if (!emplace_result.second) {
        throw std::runtime_error("New field '" + new_value + "' is duplicated in the map");
      }

      auto field_position = std::distance(origin_fields.begin(), std::find(origin_fields.begin(), origin_fields.end(), old_value));
      fields_after_rename.at(field_position) = new_value;
    }

    {
      std::set<std::string> tmp(fields_after_rename.begin(), fields_after_rename.end());
      if (tmp.size() < fields_after_rename.size()) {
        throw std::runtime_error("Duplicated fields after rename");
      }
    }

    auto missing_args = Details::GetMissingArgs(fields_to_rename, origin_fields);
    Details::ThrowMissingArgs(missing_args);

    return std::make_shared<RenameFieldsActionResultImpl>(fields_after_rename, new_to_old_map, origin);
  }

 private:
  std::map<std::string, std::string> old_to_new_map_;
};

}// namespace BranchViewAction

}// namespace AnalysisTree
#endif//ANALYSISTREE_INFRA_BRANCHVIEW_HPP
