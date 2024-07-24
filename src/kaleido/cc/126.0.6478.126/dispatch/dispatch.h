#ifndef DISPATCH_H_
#define DISPATCH_H_

#include <queue>

#include "components/devtools/simple_devtools_protocol_client/simple_devtools_protocol_client.h"

#include "base/task/sequenced_task_runner.h"
#include "headless/app/kaleido.h"
#include <unordered_map>
#include "base/environment.h"
#include "base/files/file_util.h"

namespace kaleido {
  using namespace simple_devtools_protocol_client;

  class Kaleido;


  class Tab {
    public:
      Tab();
      ~Tab();
      // should disable other assignments
      base::raw_ptr<headless::HeadlessWebContents> web_contents_; // not ours
      std::unique_ptr<SimpleDevToolsProtocolClient> client_;
  }; // client has a disconnect, web_contents has a close, this might help

  class Job {
    public:
      Job();
      ~Job();
      // should disable other assignments
      int version;
      int id;
      std::string format;
      std::string scope;
      std::unique_ptr<Tab> currentTab;
      SimpleDevToolsProtocolClient::EventCallback runtimeEnableCb;
      std::vector<std::string>::iterator scriptItr;
    // TOOD what else, dump that initial job message
  };

  // probably should be a singleton, could use static, make_unique, etc
  // Sadly, callback hell persists in google's chromium. 
  // DevTools is an asynchronous IPC messaging platform, their internal API uses callbacks,
  // not blockable coroutines- just callbacks without async/await to linearize
  // the architecture. So how do we make it easier to read? I can't use lambda functions because 
  // lambda functions + class methods don't mix w/ google's callback utilities.
  // A full state machine that manages callbacks as subroutines would 
  // be absurdly out of scope. (note added later: chromium forces it)
  //
  // Ergo, patterns like createTab1_desc(), createTab2_desc() clarify the concepts,
  // the process started by a CreateTab() public call.
  // 
  class Dispatch {
    public:
      Dispatch(raw_ptr<Kaleido> parent_);
      ~Dispatch() = delete;

      Dispatch(const Dispatch&) = delete;
      Dispatch& operator=(const Dispatch&) = delete;
      void CreateTab(int id, const GURL &url);
      void PostJob(std::unique_ptr<Job>);
      void ReloadAll();

      void Release() {
        browser_devtools_client_.DetachClient();
        // go through tab and active jobs, maybe have to cancel stuff
      }


    private:


      raw_ptr<Kaleido> parent_;
      // a devtools client for the _whole_ browser process (not a tab)
      SimpleDevToolsProtocolClient browser_devtools_client_;

      // Represent connections to a tab
      std::queue<std::unique_ptr<Tab>> tabs;
      std::queue<std::unique_ptr<Job>> jobs;
      int job_number = 0;

      std::unordered_map<int, std::unique_ptr<Job>> activeJobs; // TODO needs to transfer everthing

      // All queue operations happen on a SequencedTaskRunner for memory safety
      // Note: no callbacks allowed from within the SequencedTaskRunner
      scoped_refptr<base::SequencedTaskRunner> job_line;

      void runJob1_resetTab(const int &job_id);
      void runJob2_reloadTab(const int &job_id, base::Value::Dict msg);
      void runJob3_loadScripts(const int &job_id, const base::Value::Dict& msg);
      void runJob4_loadNextScript(const int &job_id, const base::Value::Dict msg);
      void runJob5_runLoadedScript(const int job_id, const base::Value::Dict msg);

      void sortTab(int id, std::unique_ptr<Tab> tab);
      void sortJob(std::unique_ptr<Job>);
      void dispatchJob(std::unique_ptr<Job> job, std::unique_ptr<Tab> tab);
      void dumpEvent(const base::Value::Dict& msg);
      void dumpResponse(base::Value::Dict msg);
      void reloadAll();

      bool popplerAvailable;
      bool inkscapeAvailable;
      std::unique_ptr<base::Environment> env;
  };
}

#endif  // DISPATCH_H_
