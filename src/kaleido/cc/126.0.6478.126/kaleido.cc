// kaleido.cc:
//  goals:
//    * start up the browser
//    * start up the tab manager
//    * start up the IO thread

#include <signal.h>
#include <cstdio>
#include <string>

#include "headless/app/kaleido.h"

// Browser stuff
#include "headless/lib/browser/headless_browser_impl.h"
#include "headless/public/headless_browser.h"
#include "headless/public/headless_browser_context.h"

// Derp
#include "base/logging.h"

// Callbacks and threads
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"

// For JS
#include "third_party/abseil-cpp/absl/types/optional.h"
#include <iostream>
#include "base/json/json_reader.h"

#include "headless/app/scopes/Factory.h"
// For copy 1
#include "base/command_line.h"

/// COPY 2
#include "base/files/file_util.h"
#include "base/strings/stringprintf.h"
#include <iostream>
#include <fstream>

// This is from the original kaleido
namespace kaleido {
    namespace utils {
        // Load version string

        void writeJsonMessage(int code, std::string message) {
            static std::string *version = nullptr;
            if (!version) {
                std::ifstream verStream("version");
                version = new std::string((
                            std::istreambuf_iterator<char>(verStream)),std::istreambuf_iterator<char>());
            }
            std::string error = base::StringPrintf(
                    "{\"code\": %d, \"message\": \"%s\", \"result\": null, \"version\": \"%s\"}\n",
                    code, message.c_str(), version->c_str());
            std::cout << error;
        }
    }
}


/// END COPY 2
namespace kaleido {

Kaleido::Kaleido() = default; // Define here or else chromium complains.

// Control Flow, declare here
void Kaleido::ShutdownSoon() {
  browser_->BrowserMainThread()->PostTask(
      FROM_HERE,
      base::BindOnce(&Kaleido::ShutdownTask, base::Unretained(this)));
}
void Kaleido::ShutdownTask() {
  LOG(INFO) << "Calling shutdown on browser";
  dispatch->Release(); // Fine to destruct what we have here.
  browser_.ExtractAsDangling()->Shutdown();
}

void Kaleido::OnBrowserStart(headless::HeadlessBrowser* browser) {
  browser_ = browser; // global by another name

  // Actual constructor duties, init stuff
  output_sequence = base::ThreadPool::CreateSequencedTaskRunner(
      {base::TaskPriority::BEST_EFFORT, base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN}
    ); // Can't do this before OnBrowserStart!

  dispatch = new Dispatch(this); // Tab manager

  // Create browser context and set it as the default. The default browser
  // context is used by the Target.createTarget() DevTools command when no other
  // context is given.
  // This stuff has weird side effects and I'm not sure its necessary.
  headless::HeadlessBrowserContext::Builder context_builder = browser_->CreateBrowserContextBuilder();
  context_builder.SetIncognitoMode(true);
  headless::HeadlessBrowserContext* browser_context = context_builder.Build();
  browser_->SetDefaultBrowserContext(browser_context);

  // A note about this strategy:
    // I don't see the point of accepting information on the commandline and on stdin, but I think
    // it came from not really knowing how to build a messaging interface on stdin. IPC on one channel,
    // unless you have a good reason. There is no good reason here, its just a switch.
    // Either way, the whole concepts of scopes is wildly overengineered and should be entirely eliminated.
    // It is essentially a plugin system created for the sake of having a "kaleido" which accepts "scopes" as 
    // a project description.
    // A very nice sounding structure, but expensive work for the sake of some catch phrases when a much simpler
    // solution would get the job done.
    // The scopes should be eliminated, and we should not accept pull requests for third parties integrating their work
    // if it generates work for us (bug fixes are fine but don't invite plugins to review)
    // Kaleido should open browser tabs to an arbitrary file and send javascript fragments to
    // the browser tab which generate and download the image. This structure would be more flexible, allow people use to kaleido
    // as an import for their own project, and eliminate 3 entire files and another 200 lines of code.
    // I would love to do that, but I feel this project is so tied to its advertising with its fancy name, that I left in all this
    // extremely overengineerd, counterproductive boilerplate.
  // BEGIN COPY 1
  // Get the URL from the command line.
  base::CommandLine::StringVector args =
          base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.empty()) {
      kaleido::utils::writeJsonMessage(1, "No Scope Specified");
      browser->Shutdown();
      exit(EXIT_FAILURE);
  }
  // Get first command line argument as a std::string using a string stream.
  // This handles the case where args[0] is a wchar_t on Windows
  std::stringstream scope_stringstream;
  scope_stringstream << args[0];
  std::string scope_name = scope_stringstream.str();

  // Instantiate renderer scope
  kaleido::scopes::BaseScope *scope_ptr = LoadScope(scope_name);

  if (!scope_ptr) {
      // Invalid scope name
      kaleido::utils::writeJsonMessage(1,  base::StringPrintf("Invalid scope: %s", scope_name.c_str()));
      browser->Shutdown();
      exit(EXIT_FAILURE);
  } else if (!scope_ptr->errorMessage.empty()) {
      kaleido::utils::writeJsonMessage(1,  scope_ptr->errorMessage);
      browser->Shutdown();
      exit(EXIT_FAILURE);
  }

  // Add javascript bundle
  scope_ptr->localScriptFiles.emplace_back("./js/kaleido_scopes.js");

  // Build initial HTML file
  std::list<std::string> scriptTags = scope_ptr->ScriptTags();
  std::stringstream htmlStringStream;
  htmlStringStream << "<html><head><meta charset=\"UTF-8\"><style id=\"head-style\"></style>";

  // Add script tags
  while (!scriptTags.empty()) {
      std::string tagValue = scriptTags.front();
      GURL tagUrl(tagValue);
      if (tagUrl.is_valid()) {
          // Value is a url, use a src of script tag
          htmlStringStream << "<script type=\"text/javascript\" src=\"" << tagValue << "\"></script>";
      } else {
          // Value is not a url, use a inline JavaScript code
          htmlStringStream << "<script>" << tagValue << "</script>\n";
      }
      scriptTags.pop_front();
  }
  // Close head and add body with img tag place holder for PDF export
  htmlStringStream << "</head><body style=\"{margin: 0; padding: 0;}\"><img id=\"kaleido-image\"><img></body></html>";

  // Write html to temp file
  std::string tmpFileName = std::tmpnam(nullptr) + std::string(".html");
  std::ofstream htmlFile;
  htmlFile.open(tmpFileName, std::ios::out);
  htmlFile << htmlStringStream.str();
  htmlFile.close();

  // Create file:// url to temp file
  GURL url = GURL(std::string("file://") + tmpFileName);

  // Initialization succeeded
  kaleido::utils::writeJsonMessage(0, "Success");

  // END COPY 1
  // TODO, we need to store stuff here, but we'll come back as we use them

  // Run
  browser_->BrowserMainThread()->PostTask(
      FROM_HERE,
      base::BindOnce(&Dispatch::CreateTab, base::Unretained(dispatch), -1, ""));
  // PART OF copy 1
  for (std::string const &s: scope_ptr->LocalScriptFiles()) {
    localScriptFiles.push_back(s);
  }
  base::GetCurrentDirectory(&cwd);
  // END THAT
  // We need to get here from the compiler, we probably need to see if we can package and use it.
  // Lets see how far we get if we load it manually with plotly scope (that's an argument)
  // Then lets see how we actually call it from python and what python gives us back, if it accepts our cusotm messages
  // If not, silence them, or modify python to allow it (would probably be helpful)
  // Check out what happens if we reponse differently (1, "Failure")
  // check out if we can get done reloading events

  // We don't need to use exactly their process for loading files.
  // We can create an export job that reloads a page, wait for an event, does whatever this thing was already gonna do, and then goes forward.
  StartListen();

}

// Wish this were a lambda (as in PostEcho) but would have no access to private vars
void Kaleido::listenTask() {
  std::string in;
  if (!std::getline(std::cin, in).good()) {
    LOG(WARNING) << in << ": "
      << (std::cin.eof() ? "EOF | " : "")
      << (std::cin.eof() ? "BAD | " : "GOOD | ")
      << (std::cin.eof() ? "FAIL" : "SUCCESS");
    ShutdownSoon();
    return;
  };
  if (ReadJSON(in)) postListenTask();
}

void Kaleido::postListenTask() {
  base::ThreadPool::PostTask(
    FROM_HERE, {
      base::TaskPriority::BEST_EFFORT,
      base::MayBlock(),
      base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
    base::BindOnce(&Kaleido::listenTask, base::Unretained(this))
    );
}
void Kaleido::StartListen() {
  if(listening.test_and_set(std::memory_order_relaxed)) return;
  postListenTask();
}

void Kaleido::PostEchoTask(const std::string &msg) {
  auto echo = [](const std::string &msg){ std::cout << msg << std::endl; };
  output_sequence->PostTask(FROM_HERE, base::BindOnce(echo, msg));
}


bool Kaleido::ReadJSON(std::string &msg) {
  absl::optional<base::Value> json = base::JSONReader::Read(msg);
  if (!json) {
    LOG(WARNING) << "Recieved invalid JSON from client connected to Kaleido:";
    LOG(WARNING) << msg;
    Api_ErrorInvalidJSON();
    return true;
  }
  base::Value::Dict &jsonDict = json->GetDict();
  absl::optional<int> id = jsonDict.FindInt("id");
  std::string *operation = jsonDict.FindString("operation");
  // The only operation we handle here. We're shutting down.
  // Trust chromium to handle it all when the browser exits
  // Doesn't need id, no return
  if (operation && *operation == "shutdown") {
    ShutdownSoon();
    return false; // breaks stdin loop
  }
  if (!operation || !id) {
    Api_ErrorMissingBasicFields(id);
    return true;
  }
  if (*id < 0) {
    Api_ErrorNegativeId(*id);
    return true;
  }
  if (messageIds.find(*id) != messageIds.end()) {
    Api_ErrorDuplicateId(*id);
    return true;
  }

  if (*operation == "create_tab") {
      browser_->BrowserMainThread()->PostTask(
          FROM_HERE,
          base::BindOnce(&Dispatch::CreateTab, base::Unretained(dispatch), *id, ""));
  } else if (*operation == "noop") {} else {
    Api_ErrorUnknownOperation(*id, *operation);
    return true;
  }


  messageIds.emplace(*id, *operation);
  return true;
}

void Kaleido::ReportOperation(int id, bool success, base::Value::Dict msg) {
  if (!success && id < 0) {
    LOG(ERROR) << "Failure of internal dev tools operation id "
      << std::to_string(id)
      << " and msg: "
      << msg;
    return;
  }
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"success":)"+std::to_string(success)+R"(, "msg":)"+msg.DebugString()+R"(})");
}
void Kaleido::ReportFailure(int id, const std::string& msg) {
  if (id < 0) {
    LOG(ERROR) << "Failure of internal dev tools operation id "
      << std::to_string(id)
      << " and msg: "
      << msg;
    return;
  }
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"success":false, "msg":")"+msg+R"("})");
}

void Kaleido::ReportSuccess(int id) {
  if (id < 0) return;
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"success":true})");
}

void Kaleido::Api_ErrorInvalidJSON() {
  PostEchoTask(R"({"error":"malformed JSON string"})");
}

void Kaleido::Api_ErrorMissingBasicFields(absl::optional<int> id) {
  if (id) {
    PostEchoTask(R"({"id":)"+std::to_string(*id)+R"(,"error":"all messages must contain an 'id' integer and an 'operation' string"})");
  } else {
    PostEchoTask(R"({"error":"all messages must contain an 'id' integer and an 'operation' string"})");
  }
}

void Kaleido::Api_ErrorDuplicateId(int id) {
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"error":"message using already-used 'id' integer"})");
}

void Kaleido::Api_ErrorNegativeId(int id) {
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"error":"must use 'id' integer >=0"})");
}

void Kaleido::Api_ErrorUnknownOperation(int id, const std::string& op) {
  PostEchoTask(R"({"id":)"+std::to_string(id)+R"(,"error":"Unknown operation:)"+op+R"("})");
}

} // namespace kaleido

