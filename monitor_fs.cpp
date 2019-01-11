#include <iostream>
#include <map>
#include <string>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/inotify.h>
#include <linux/limits.h>

// OR of all inotify flag bits
#define IN_ALL (IN_ACCESS | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | \
                IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | \
                IN_MOVED_FROM | IN_MOVED_TO | IN_OPEN | IN_IGNORED | IN_ISDIR | \
                IN_Q_OVERFLOW | IN_UNMOUNT)

// Flag to be set when CTRL-C is pressed by user
static volatile bool gblExitRequested = false;

#define UNUSED(x) (void)x

static void handle_signals(int s)
{
	UNUSED(s);
	gblExitRequested = true;
}

static void dump_inotify_event(const struct inotify_event *const iNotifyEvent)
{
	// Map inotify flag bit to string representation
	#define MAP_PAIR(x) { x, #x },
	static std::map<uint32_t, std::string> codes = {
		MAP_PAIR(IN_ACCESS)        // File accessed
		MAP_PAIR(IN_ATTRIB)        // Meta data changed
		MAP_PAIR(IN_CLOSE_WRITE)   // File opened for write closed
		MAP_PAIR(IN_CLOSE_NOWRITE) // File/dir not opened for write closed
		MAP_PAIR(IN_CREATE)        // File/dir created
		MAP_PAIR(IN_DELETE)        // File/dir deleted
		MAP_PAIR(IN_DELETE_SELF)   // Watched file/dir itself deleted
		MAP_PAIR(IN_MODIFY)        // File modified
		MAP_PAIR(IN_MOVE_SELF)     // Watched file/dir itself moved
		MAP_PAIR(IN_MOVED_FROM)    // Old dir when file moved
		MAP_PAIR(IN_MOVED_TO)      // New dir when file moved
		MAP_PAIR(IN_OPEN)          // File/dir opened
		MAP_PAIR(IN_IGNORED)       // Watch was removed
		MAP_PAIR(IN_ISDIR)         // Subject of this event is a directory
		MAP_PAIR(IN_Q_OVERFLOW)    // Event queue overflowed (wd is -1)
		MAP_PAIR(IN_UNMOUNT)       // Filesystem containing watched object was unmounted
	};
	#undef MAP_PAIR

	std::cout << "Event info:\n";
	std::cout << "   Watch descriptor.... " << iNotifyEvent->wd << "\n";
	std::cout << "   Mask................ " << iNotifyEvent->mask << "\n";
	std::cout << "   Cookie.............. " << iNotifyEvent->cookie << "\n";
	std::cout << "   Length of name...... " << iNotifyEvent->len << "\n";
	std::cout << "   Name................ " << iNotifyEvent->name <<"\n";

	std::cout << "Event mask includes:\n";
	for (auto it=codes.begin(); it!=codes.end(); ++it)
	{
		if (iNotifyEvent->mask & it->first)
		{
			std::cout << "   - " << it->second << "\n";
		}
	}

	std::cout << std::endl;
}

int main(int argc, char *argv[])
{
	struct sigaction sigIntHandler;
	int watchDescriptor;
	ssize_t bytesRead;
	int failure = 0;
	int inotifyFd = 1;
	
	if (argc != 2)
	{
		std::cerr << "Only one argument required: Directory or file!" << std::endl;
		return 1;
	}

	// Allocate a buffer large enough to contain at least one inotify_event
	const size_t minBufferSize = sizeof(struct inotify_event) + NAME_MAX + 1;
	std::unique_ptr<char[]> uptrBuffer {new char[minBufferSize]};

	// Install CTRL-C handler
	sigIntHandler.sa_handler = handle_signals;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;
	sigaction(SIGINT, &sigIntHandler, NULL);


	// Get an inotify file descriptor to read
	inotifyFd = inotify_init();
	if (inotifyFd < 0)
	{
		std::cerr << "Failed to create inotify file descriptor" << std::endl;
		return 1;
	}

	// Add argv[1] as a file/dir to be watched. Get back a watch descriptor.
	watchDescriptor = inotify_add_watch(inotifyFd, argv[1], IN_ALL);
	if (watchDescriptor < 0)
	{
		std::cerr << "Failed to add the directory '" << argv[1] 
		          << "' to the watch list" << std::endl;
		close(inotifyFd);
		return 1;
	}

	while(!gblExitRequested)
	{
		// Read 1 or more inotify events. The buffer is big enough for at least
		// one event, but it could fit many events depending on the length
		// of the dir/filename 
		char *buffer = uptrBuffer.get();
		bytesRead = read(inotifyFd, buffer, minBufferSize);
		if (bytesRead < 0)
		{
			if (errno == EINTR) continue;
			else                break;
		}
		else if (static_cast<size_t>(bytesRead) < sizeof(struct inotify_event))
		{
			break;
		}

		std::cout << "Completed one read..." << std::endl;

		const char *const bufferEnd = buffer + bytesRead;
		while(buffer < bufferEnd)
		{
			struct inotify_event *iNotifyEvent = reinterpret_cast<struct inotify_event *>(buffer);
			dump_inotify_event(iNotifyEvent);
			buffer += sizeof(struct inotify_event) + iNotifyEvent->len;
		}
	}

	if (errno != EINTR)
	{
		if (bytesRead < 0)
		{
			std::cerr << "Error reading inotify file id" << std::endl;
			std::cerr << "Errno is '" << strerror(errno) << "' (" << errno << ")" << std::endl;
			failure = 1;
		}
		else if (static_cast<size_t>(bytesRead) != sizeof(struct inotify_event))
		{
			std::cerr << "Error reading inotify file id. Unexpected number of bytes" << std::endl;
			failure = 1;
		}
	}

	std::cout << "Ending program..." << std::endl;
	inotify_rm_watch(inotifyFd, watchDescriptor);
	close(inotifyFd);

	return failure;
}