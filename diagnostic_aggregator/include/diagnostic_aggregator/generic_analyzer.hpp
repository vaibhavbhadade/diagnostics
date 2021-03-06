// Copyright 2015 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef DIAGNOSTIC_AGGREGATOR__GENERIC_ANALYZER_HPP_
#define DIAGNOSTIC_AGGREGATOR__GENERIC_ANALYZER_HPP_

#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "pluginlib/class_list_macros.hpp"
#include "diagnostic_aggregator/analyzer.hpp"
#include "rclcpp/rclcpp.hpp"
#include "diagnostic_aggregator/generic_analyzer_base.hpp"
#include "diagnostic_aggregator/status_item.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"

// TODO(tfoote replace these terrible macros)
#define ROS_ERROR printf
#define ROS_FATAL printf
#define ROS_WARN printf
#define ROS_INFO printf

namespace diagnostic_aggregator
{

/*!
 *\brief Returns list of strings from a parameter
 *
 * Given an XmlRpcValue, gives vector of strings of that parameter
 *\return False if XmlRpcValue is not string or array of strings
 */
inline bool getParamVals(
  std::string & subject,
  std::vector<std::string> & container)
{
  container.clear();
  char chars[] = "[]";
  char * p;

  for (unsigned int i = 0; i < strlen(chars); ++i) {
    // you need include <algorithm> to use general algorithms like std::remove()
    subject.erase(std::remove(subject.begin(), subject.end(), chars[i]),
      subject.end());
  }
  size_t len = subject.length() + 1;
  char * s = new char[len];
  memset(s, 0, len * sizeof(char));
  memcpy(s, subject.c_str(), (len - 1) * sizeof(char));
  char * rest = s;
//   for (char * p = strtok(s, ","); p != NULL; p = strtok(NULL, ",")) {
  while ((p = strtok_r(rest, ",", &rest))) {
    container.push_back(p);
  }
  delete[] s;

  return true;
}

/*!
 *\brief GenericAnalyzer is most basic diagnostic Analyzer
 *
 *  GenericAnalyzer analyzes a segment of diagnostics data and reports
 * processed diagnostics data. All analyzed status messages are prepended with
 * "Base Path/My Path", where "Base Path" is from the parent of this Analyzer,
 * (ex: 'PR2') and "My Path" is from this analyzer (ex: 'Power System').
 *
 * The GenericAnalyzer is initialized as a plugin by the diagnostic Aggregator.
 * Following is an example of the necessary parameters of the GenericAnalyzer.
 * See the Aggregator class for more information on loading Analyzer plugins.
 *\verbatim
 * my_namespace:
 *   type: GenericAnalyzer
 *   path: My Path
 *\endverbatim
 * Required Parameters:
 * - \b type This is the class name of the analyzer, used to load the correct
 *plugin type.
 * - \b path All diagnostic items analyzed by the GenericAnalyzer will be under
 *"Base Path/My Path".
 *
 * In the above example, the GenericAnalyzer wouldn't analyze anything. The
 *GenericAnalyzer must be configured to listen to diagnostic status names. To do
 *this, optional parameters, like "contains", will tell the analyzer to analyze
 *an item that contains that value. The GenericAnalyzer looks at the name of the
 *income diagnostic_msgs/DiagnosticStatus messages to determine item matches.
 *
 * Optional Parameters for Matching:
 * - \b contains Any item that contains these values
 * - \b startswith Item name must start with this value
 * - \b name Exact name match
 * - \b expected Exact name match, will warn if not present
 * - \b regex Regular expression (regex) match against name
 * The above parameters can be given as a single string ("tilt_hokuyo_node") or
 *a list of strings (['Battery', 'Smart Battery']).
 *
 * In some cases, it's possible to clean up the processed diagnostic status
 *names.
 * - \b remove_prefix If these prefix is found in a status name, it will be
 *removed in the output. Can be given as a string or list of strings.
 *
 * The special parameter '''find_and_remove_prefix''' combines "startswith" and
 * "remove_prefix". It can be given as a string or list of strings.
 *
 * If the number of incoming items under a GenericAnalyzer is known, use
 *'''num_items''' to set an exact value. If the number of items that matches the
 *above parameters is incorrect, the GenericAnalyzer will report an error in the
 *top-level status. This is
 * "-1" by default. Negative values will not cause a check on the number of
 *items.
 *
 * For tracking stale items, use the "timeout" parameter. Any item that doesn't
 *update within the timeout will be marked as "Stale", and will cause an error
 *in the top-level status. Default is 5.0 seconds. Any value <0 will cause stale
 *items to be ignored.
 *
 * The GenericAnalyzer can discard stale items. Use the "discard_stale"
 *parameter to remove any items that haven't updated within the timeout. This is
 *"false" by default.
 *
 * Example configurations:
 *\verbatim
 * hokuyo:
 *   type: GenericAnalyzer
 *   path: Hokuyo
 *   find_and_remove_prefix: hokuyo_node
 *   num_items: 3
 *\endverbatim
 *
 *\verbatim
 * power_system:
 *   type: GenericAnalyzer
 *   path: Power System
 *   startswith: [
 *     'Battery',
 *     'IBPS']
 *   expected: Power board 1000
 *   dicard_stale: true
 *\endverbatim
 *
 * \subsubsection GenericAnalyzer Behavior
 *
 * The GenericAnalyzer will report the latest status of any item that is should
 *analyze. It will report a separate diagnostic_msgs/DiagnosticStatus with the
 *name "Base Path/My Path". This "top-level" status will have the error state of
 *the highest of its children.
 *
 * Stale items are handled differently. A stale child will cause an error
 * in the top-level status, but if all children are stale, the top-level status
 *will be stale.
 *
 * Example analyzer behavior, using the "Hokuyo" configuration above:
 *\verbatim
 * Input - (DiagnosticStatus Name, Error State)
 * hokuyo_node: Connection Status, OK
 * hokuyo_node: Frequency Status, Warning
 * hokuyo_node: Driver Status, OK
 *
 * Output - (DiagnosticStatus Name, Error State)
 * Hokuyo, Warning
 * Hokuyo/Connection Status, OK
 * Hokuyo/Frequency Status, Warning
 * Hokuyo/Driver Status, OK
 *\endverbatim
 *
 *
 */
class GenericAnalyzer : public GenericAnalyzerBase
{
public:
  /*!
   *\brief Default constructor loaded by pluginlib
   */
  GenericAnalyzer();

  virtual ~GenericAnalyzer();

  // Move to class description above
  /*!
   *\brief Initializes GenericAnalyzer from namespace. Returns true if s
   *
   *\param base_path : Prefix for all analyzers (ex: 'Robot')
   *\param n : NodeHandle in full namespace
   *\return True if initialization succeed, false if no errors of
   */
  //  bool init(const std::string base_path, const rclcpp::Node::SharedPtr &n);
  bool init(
    const std::string base_path, const char * nsp,
    const rclcpp::Node::SharedPtr & nh, const char * rns);
  bool init_sc(
    const std::string base_path, const char * nsp,
    const rclcpp::Node::SharedPtr & nh, const char * rns);

  /*!
   *\brief Reports current state, returns vector of formatted status messages
   *
   *\return Vector of DiagnosticStatus messages, with correct prefix for all
   *names.
   */
  virtual std::vector<std::shared_ptr<diagnostic_msgs::msg::DiagnosticStatus>>
  report();

  /*!
   *\brief Returns true if item matches any of the given criteria
   *
   */
  virtual bool match(const std::string name);

private:
  std::vector<std::string> chaff_; /**< Removed from the start of node names. */
  std::vector<std::string> expected_;
  std::vector<std::string> startswith_;
  std::vector<std::string> contains_;
  std::vector<std::string> name_;
  std::vector<std::regex>
  regex_;     /**< Regular expressions to check against diagnostics names. */
  rclcpp::Node::SharedPtr gen_nh;
};

}  // namespace diagnostic_aggregator
#endif  // DIAGNOSTIC_AGGREGATOR__GENERIC_ANALYZER_HPP_`
