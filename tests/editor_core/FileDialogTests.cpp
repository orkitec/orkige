/**************************************************************
	created:	2026/07/07 at 12:00
	filename: 	FileDialogTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the native-file-dialog result marshaling
	(tools/editor/FileDialog.{h,cpp}): SDL3's dialog callbacks may fire
	on another thread, so they only deposit a FileDialogResult in the
	FileDialogResultQueue mailbox and the editor's main loop consumes it
	once per frame. These tests pin down those consume semantics.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <FileDialog.h>

#include <thread>

TEST_CASE("an empty dialog queue has nothing to consume",
	"[editor][filedialog]")
{
	Orkige::FileDialogResultQueue queue;
	Orkige::FileDialogResult result;
	REQUIRE_FALSE(queue.consume(result));
}

TEST_CASE("a delivered dialog result is consumed exactly once",
	"[editor][filedialog]")
{
	Orkige::FileDialogResultQueue queue;
	Orkige::FileDialogResult delivered;
	delivered.action = Orkige::FileDialogAction::OpenScene;
	delivered.accepted = true;
	delivered.path = "/tmp/some.oscene";
	queue.deliver(delivered);

	Orkige::FileDialogResult consumed;
	REQUIRE(queue.consume(consumed));
	REQUIRE(consumed.action == Orkige::FileDialogAction::OpenScene);
	REQUIRE(consumed.accepted);
	REQUIRE_FALSE(consumed.failed);
	REQUIRE(consumed.path == "/tmp/some.oscene");

	// the slot is empty again - the main loop must not act twice on one
	// dialog interaction
	REQUIRE_FALSE(queue.consume(consumed));
}

TEST_CASE("a cancelled dialog is delivered as a decided no-op",
	"[editor][filedialog]")
{
	// SDL reports cancel as an empty filelist: neither accepted nor failed,
	// but the result must still flow through (it closes out the interaction)
	Orkige::FileDialogResultQueue queue;
	Orkige::FileDialogResult cancelled;
	cancelled.action = Orkige::FileDialogAction::SaveSceneAs;
	queue.deliver(cancelled);

	Orkige::FileDialogResult consumed;
	REQUIRE(queue.consume(consumed));
	REQUIRE(consumed.action == Orkige::FileDialogAction::SaveSceneAs);
	REQUIRE_FALSE(consumed.accepted);
	REQUIRE_FALSE(consumed.failed);
	REQUIRE(consumed.path.empty());
	REQUIRE_FALSE(queue.consume(consumed));
}

TEST_CASE("a failed dialog carries its error message through",
	"[editor][filedialog]")
{
	Orkige::FileDialogResultQueue queue;
	Orkige::FileDialogResult failed;
	failed.action = Orkige::FileDialogAction::ImportMesh;
	failed.failed = true;
	failed.errorMessage = "File dialog driver unavailable";
	queue.deliver(failed);

	Orkige::FileDialogResult consumed;
	REQUIRE(queue.consume(consumed));
	REQUIRE(consumed.failed);
	REQUIRE_FALSE(consumed.accepted);
	REQUIRE(consumed.errorMessage == "File dialog driver unavailable");
}

TEST_CASE("an unconsumed dialog result is overwritten - latest wins",
	"[editor][filedialog]")
{
	// the editor opens one dialog at a time; if a stale callback ever
	// straggles in on top of a fresh result, the fresh one must win and
	// only ONE result may come out
	Orkige::FileDialogResultQueue queue;
	Orkige::FileDialogResult first;
	first.action = Orkige::FileDialogAction::OpenScene;
	first.accepted = true;
	first.path = "/tmp/stale.oscene";
	queue.deliver(first);

	Orkige::FileDialogResult second;
	second.action = Orkige::FileDialogAction::ImportMesh;
	second.accepted = true;
	second.path = "/tmp/fresh.glb";
	queue.deliver(second);

	Orkige::FileDialogResult consumed;
	REQUIRE(queue.consume(consumed));
	REQUIRE(consumed.action == Orkige::FileDialogAction::ImportMesh);
	REQUIRE(consumed.path == "/tmp/fresh.glb");
	REQUIRE_FALSE(queue.consume(consumed));
}

TEST_CASE("a result delivered from another thread reaches the consumer",
	"[editor][filedialog]")
{
	// the whole point of the mailbox: SDL may run the dialog callback on a
	// worker thread while the main loop consumes on the main thread
	Orkige::FileDialogResultQueue queue;
	std::thread callbackThread([&queue]()
	{
		Orkige::FileDialogResult result;
		result.action = Orkige::FileDialogAction::SaveSceneAs;
		result.accepted = true;
		result.path = "/tmp/threaded.oscene";
		queue.deliver(result);
	});
	callbackThread.join();

	Orkige::FileDialogResult consumed;
	REQUIRE(queue.consume(consumed));
	REQUIRE(consumed.action == Orkige::FileDialogAction::SaveSceneAs);
	REQUIRE(consumed.path == "/tmp/threaded.oscene");
}
