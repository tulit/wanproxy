#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <common/buffer.h>

#include <event/action.h>
#include <event/callback.h>
#include <event/event_system.h>

EventPoll::EventPoll(void)
: log_("/event/poll"),
  read_poll_(),
  write_poll_(),
  kq_(kqueue())
{
	ASSERT(kq_ != -1);
}

EventPoll::~EventPoll()
{
	ASSERT(read_poll_.empty());
	ASSERT(write_poll_.empty());
}

Action *
EventPoll::poll(const Type& type, int fd, EventCallback *cb)
{
	ASSERT(fd != -1);

	EventPoll::PollHandler *poll_handler;
	struct kevent kev;
	switch (type) {
	case EventPoll::Readable:
		ASSERT(read_poll_.find(fd) == read_poll_.end());
		poll_handler = &read_poll_[fd];
		EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
		break;
	case EventPoll::Writable:
		ASSERT(write_poll_.find(fd) == write_poll_.end());
		poll_handler = &write_poll_[fd];
		EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
		break;
	default:
		NOTREACHED();
	}
	int evcnt = ::kevent(kq_, &kev, 1, NULL, 0, NULL);
	if (evcnt == -1)
		HALT(log_) << "Could not add event to kqueue.";
	ASSERT(evcnt == 0);
	ASSERT(poll_handler->action_ == NULL);
	poll_handler->callback_ = cb;
	Action *a = new EventPoll::PollAction(this, type, fd);
	return (a);
}

void
EventPoll::cancel(const Type& type, int fd)
{
	EventPoll::PollHandler *poll_handler;

	struct kevent kev;
	switch (type) {
	case EventPoll::Readable:
		ASSERT(read_poll_.find(fd) != read_poll_.end());
		poll_handler = &read_poll_[fd];
		poll_handler->cancel();
		read_poll_.erase(fd);
		EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
		break;
	case EventPoll::Writable:
		ASSERT(write_poll_.find(fd) != write_poll_.end());
		poll_handler = &write_poll_[fd];
		poll_handler->cancel();
		write_poll_.erase(fd);
		EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
		break;
	}
	int evcnt = ::kevent(kq_, &kev, 1, NULL, 0, NULL);
	if (evcnt == -1)
		HALT(log_) << "Could not delete event from kqueue.";
	ASSERT(evcnt == 0);
}

bool
EventPoll::idle(void) const
{
	return (read_poll_.empty() && write_poll_.empty());
}

void
EventPoll::poll(void)
{
	return (wait(0));
}

void
EventPoll::wait(int secs)
{
	static const unsigned kevcnt = 128;
	struct timespec ts;

	if (idle())
		return;

	ts.tv_sec = secs;
	ts.tv_nsec = 0;

	struct kevent kev[kevcnt];
	int evcnt = kevent(kq_, NULL, 0, kev, kevcnt, secs == -1 ? NULL : &ts);
	if (evcnt == -1)
		HALT(log_) << "Could not poll kqueue.";
	int i;
	for (i = 0; i < evcnt; i++) {
		struct kevent *ev = &kev[i];
		EventPoll::PollHandler *poll_handler;
		switch (ev->filter) {
		case EVFILT_READ:
			ASSERT(read_poll_.find(ev->ident) != read_poll_.end());
			poll_handler = &read_poll_[ev->ident];
			break;
		case EVFILT_WRITE:
			ASSERT(write_poll_.find(ev->ident) !=
			       write_poll_.end());
			poll_handler = &write_poll_[ev->ident];
			break;
		default:
			NOTREACHED();
		}
		if ((ev->flags & EV_ERROR) != 0) {
			poll_handler->callback(Event(Event::Error, ev->fflags));
			continue;
		}
		if ((ev->flags & EV_EOF) != 0) {
			poll_handler->callback(Event(Event::EOS, ev->fflags));
			continue;
		}
		poll_handler->callback(Event(Event::Done, ev->fflags));
	}
}

void
EventPoll::wait(void)
{
	return (wait(-1));
}