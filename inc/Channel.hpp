#ifndef CHANNEL_HPP
# define CHANNEL_HPP

# include <string>
# include <set>
# include <cstddef>

class Client;

/*
** A channel: its members, its operators, its modes (owned by C).
** Modes required by the subject: i (invite-only), t (topic restricted),
** k (key), o (operator), l (user limit).
** Starting skeleton — adapt as needed and be ready to justify it.
*/
class Channel
{
	private:
		std::string			_name;
		std::string			_topic;
		std::string			_key;				// mode k ("" = no key)
		std::set<Client *>	_members;
		std::set<Client *>	_operators;			// mode o
		std::set<Client *>	_invited;			// for mode i
		bool				_inviteOnly;		// mode i
		bool				_topicRestricted;	// mode t
		std::size_t			_userLimit;			// mode l (0 = no limit)

		Channel(const Channel &other);
		Channel &operator=(const Channel &other);

	public:
		Channel(const std::string &name);
		~Channel();

		const std::string			&getName() const;
		const std::string			&getTopic() const;
		const std::string			&getKey() const;
		bool						isInviteOnly() const;
		bool						isTopicRestricted() const;
		std::size_t					getUserLimit() const;
		const std::set<Client *>	&getMembers() const;
		const std::set<Client *>	&getOperators() const;

		void						addOperator(Client *client);
		void						removeOperator(Client *client);
		bool						isOperator(Client *client) const;

		bool						isInvited(Client *client) const;
		void						invite(Client *client);

		void						setKey(const std::string &key);
		void						removeKey();
		void						setInviteOnly();
		void						removeInviteOnly();
		void						setTopicRestricted();
		void						removeTopicRestricted();
		void						setTopic(const std::string &topic);
		void						removeTopic();
		void						setUserLimit(std::size_t limit);

		void						addMember(Client *client);
		void						removeMember(Client *client);
		bool						isMember(Client *client) const;
		void						broadcast(const std::string &msg, Client *except = NULL) const;

		void						removeMembers();
		bool						isEmpty() const;
};

#endif
