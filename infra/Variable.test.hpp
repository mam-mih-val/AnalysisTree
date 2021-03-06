#ifndef ANALYSISTREE_INFRA_VARIABLE_TEST_HPP_
#define ANALYSISTREE_INFRA_VARIABLE_TEST_HPP_

#include <gtest/gtest.h>

#include <infra/Variable.hpp>

#include <core/BranchConfig.hpp>
#include <core/Configuration.hpp>

namespace{

using namespace AnalysisTree;

TEST(Test_AnalysisTreeInfra, Test_Varible) {

  BranchConfig branch_config("RecTrack", DetType::kTrack);
  branch_config.AddField<float>("test_f");
  branch_config.AddField<int>("test_i");
  branch_config.AddField<bool>("test_b");

  Configuration configuration;
  configuration.AddBranchConfig(branch_config);

  Variable var1("RecTrack", "test_f");
  Variable var2("var2", {{"RecTrack", "test_f"}, {"RecTrack", "test_i"}}, [](std::vector<double> var) { return var[0]/var[1]; });

  var1.Init(configuration);
  var2.Init(configuration);

  Track track;
  track.Init(branch_config);
  track.SetField(99.f, 0);
  track.SetField(99, 0);
  track.SetField(true, 0);

  EXPECT_FLOAT_EQ(var1.GetValue(track), 99.f);
  EXPECT_FLOAT_EQ(var2.GetValue(track), 1.f);
}

}

#endif //ANALYSISTREE_INFRA_VARIABLE_TEST_HPP_
