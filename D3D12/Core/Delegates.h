#ifndef CPP_DELEGATES
#define CPP_DELEGATES

//This enables the typedef of Delegate = SinglecastDelegate as the name "SinglecastDelegate" is deprecated
#define CPP_DELEGATES_USE_OLD_NAMING 0

#define DECLARE_DELEGATE(name, ...) \
using name = Delegate<void, __VA_ARGS__>

#define DECLARE_DELEGATE_RET(name, retValue, ...) \
using name = Delegate<retValue, __VA_ARGS__>

#define DECLARE_MULTICAST_DELEGATE(name, ...) \
using name = MulticastDelegate<__VA_ARGS__>; \
using name ## Delegate = MulticastDelegate<__VA_ARGS__>::DelegateT

#define DECLARE_EVENT(name, ownerType, ...) \
class name : public MulticastDelegate<__VA_ARGS__> \
{ \
private: \
	friend class ownerType; \
	using MulticastDelegate::Broadcast; \
	using MulticastDelegate::RemoveAll; \
	using MulticastDelegate::Remove; \
};

//Base type for delegates
template<typename RetVal, typename... Args>
class IDelegate
{
public:
	IDelegate() = default;
	virtual ~IDelegate() noexcept = default;
	virtual RetVal Execute(Args&&... args) = 0;
	virtual void* GetOwner() const
	{
		return nullptr;
	}
};

template<typename RetVal, typename... Args2>
class StaticDelegate;

template<typename RetVal, typename... Args, typename... Args2>
class StaticDelegate<RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = RetVal(*)(Args..., Args2...);

	StaticDelegate(DelegateFunction function, Args2&&... args)
		: m_Function(function), m_Payload(std::forward<Args2>(args)...)
	{}
	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}
private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return m_Function(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
	}

	DelegateFunction m_Function;
	std::tuple<Args2...> m_Payload;
};

template<typename T, typename RetVal, typename... Args2>
class RawDelegate;

template<typename T, typename RetVal, typename... Args, typename... Args2>
class RawDelegate<T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = RetVal(T::*)(Args..., Args2...);

	RawDelegate(T* pObject, DelegateFunction function, Args2&&... args)
		: m_pObject(pObject), m_Function(function), m_Payload(std::forward<Args2>(args)...)
	{}
	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}
	virtual void* GetOwner() const override
	{
		return m_pObject;
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return (m_pObject->*m_Function)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
	}

	T* m_pObject;
	DelegateFunction m_Function;
	std::tuple<Args2...> m_Payload;
};

template<typename TLambda, typename RetVal, typename... Args>
class LambdaDelegate;

template<typename TLambda, typename RetVal, typename... Args, typename... Args2>
class LambdaDelegate<TLambda, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	explicit LambdaDelegate(TLambda&& lambda, Args2&&... args) :
		m_Lambda(std::forward<TLambda>(lambda)),
		m_Payload(std::forward<Args2>(args)...)
	{}

	RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}
private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		return (RetVal)((m_Lambda)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...));
	}

	TLambda m_Lambda;
	std::tuple<Args2...> m_Payload;
};

template<typename T, typename RetVal, typename... Args>
class SPDelegate;

template<typename RetVal, typename T, typename... Args, typename... Args2>
class SPDelegate<T, RetVal(Args...), Args2...> : public IDelegate<RetVal, Args...>
{
public:
	using DelegateFunction = RetVal(T::*)(Args..., Args2...);

	SPDelegate(const std::shared_ptr<T>& pObject, DelegateFunction pFunction, Args2&&... args) :
		m_pObject(pObject),
		m_pFunction(pFunction),
		m_Payload(std::forward<Args2>(args)...)
	{
	}

	virtual RetVal Execute(Args&&... args) override
	{
		return Execute_Internal(std::forward<Args>(args)..., std::index_sequence_for<Args2...>());
	}

	virtual void* GetOwner() const override
	{
		if (m_pObject.expired() == false)
		{
			return nullptr;
		}
		else
		{
			return m_pObject.lock().get();
		}
	}

private:
	template<std::size_t... Is>
	RetVal Execute_Internal(Args&&... args, std::index_sequence<Is...>)
	{
		if (m_pObject.expired())
		{
			return RetVal();
		}
		else
		{
			std::shared_ptr<T> pPinned = m_pObject.lock();
			return (pPinned.get()->*m_pFunction)(std::forward<Args>(args)..., std::get<Is>(m_Payload)...);
		}
	}

	std::weak_ptr<T> m_pObject;
	DelegateFunction m_pFunction;
	std::tuple<Args2...> m_Payload;
};

//A handle to a delegate used for a multicast delegate
//Static ID so that every handle is unique
class DelegateHandle
{
public:
	constexpr DelegateHandle() noexcept
		: m_Id(-1)
	{
	}

	explicit DelegateHandle(bool /*generateId*/) noexcept
		: m_Id(GetNewID())
	{
	}

	~DelegateHandle() noexcept = default;
	DelegateHandle(const DelegateHandle& other) = default;
	DelegateHandle& operator=(const DelegateHandle& other) = default;

	DelegateHandle(DelegateHandle&& other) noexcept
		: m_Id(other.m_Id)
	{
		other.Reset();
	}

	DelegateHandle& operator=(DelegateHandle&& other) noexcept
	{
		m_Id = other.m_Id;
		other.Reset();
		return *this;
	}

	inline operator bool() const noexcept
	{
		return IsValid();
	}

	inline bool operator==(const DelegateHandle& other) const noexcept
	{
		return m_Id == other.m_Id;
	}

	inline bool operator<(const DelegateHandle& other) const noexcept
	{
		return m_Id < other.m_Id;
	}

	inline bool IsValid() const noexcept
	{
		return m_Id != -1;
	}

	inline void Reset() noexcept
	{
		m_Id = -1;
	}

private:
	__int64 m_Id;
	static __int64 CURRENT_ID;
	static __int64 GetNewID();
};

template<size_t MaxStackSize>
class InlineAllocator
{
public:
	//Constructor
	constexpr InlineAllocator() noexcept
		: m_Size(0)
	{
		static_assert(MaxStackSize > sizeof(void*), "MaxStackSize is smaller or equal to the size of a pointer. This will make the use of an InlineAllocator pointless. Please increase the MaxStackSize.");
	}

	//Destructor
	~InlineAllocator() noexcept
	{
		Free();
	}

	//Copy constructor
	InlineAllocator(const InlineAllocator& other)
		: m_Size(0)
	{
		if (other.HasAllocation())
		{
			memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
		}
		m_Size = other.m_Size;
	}

	//Copy assignment operator
	InlineAllocator& operator=(const InlineAllocator& other)
	{
		if (other.HasAllocation())
		{
			memcpy(Allocate(other.m_Size), other.GetAllocation(), other.m_Size);
		}
		m_Size = other.m_Size;
		return *this;
	}

	//Move constructor
	InlineAllocator(InlineAllocator&& other) noexcept
		: m_Size(other.m_Size)
	{
		other.m_Size = 0;
		if (m_Size > MaxStackSize)
		{
			std::swap(pPtr, other.pPtr);
		}
		else
		{
			memcpy(Buffer, other.Buffer, m_Size);
		}
	}

	//Move assignment operator
	InlineAllocator& operator=(InlineAllocator&& other) noexcept
	{
		Free();
		m_Size = other.m_Size;
		other.m_Size = 0;
		if (m_Size > MaxStackSize)
		{
			std::swap(pPtr, other.pPtr);
		}
		else
		{
			memcpy(Buffer, other.Buffer, m_Size);
		}
		return *this;
	}

	//Allocate memory of given size
	//If the size is over the predefined threshold, it will be allocated on the heap
	void* Allocate(const size_t size)
	{
		if (m_Size != size)
		{
			Free();
			m_Size = size;
			if (size > MaxStackSize)
			{
				pPtr = new char[size];
				return pPtr;
			}
		}
		return (void*)Buffer;
	}

	//Free the allocated memory
	void Free()
	{
		if (m_Size > MaxStackSize)
		{
			delete[] (char*)pPtr;
		}
		m_Size = 0;
	}

	//Return the allocated memory either on the stack or on the heap
	void* GetAllocation() const
	{
		if (HasAllocation())
		{
			return HasHeapAllocation() ? pPtr : (void*)Buffer;
		}
		else
		{
			return nullptr;
		}
	}

	inline bool HasAllocation() const
	{
		return m_Size > 0;
	}

	inline bool HasHeapAllocation() const
	{
		return m_Size > MaxStackSize;
	}

private:
	//If the allocation is smaller than the threshold, Buffer is used
	//Otherwise pPtr is used together with a separate dynamic allocation
	union
	{
		char Buffer[MaxStackSize];
		void* pPtr;
	};
	size_t m_Size;
};

//Delegate that can be bound to by just ONE object
template<typename RetVal, typename... Args>
class Delegate
{
public:
	using IDelegateT = IDelegate<RetVal, Args...>;

	//Default constructor
	constexpr Delegate() noexcept
	{
	}

	//Default destructor
	~Delegate() noexcept
	{
		Release();
	}

	//Copy contructor
	Delegate(const Delegate& other)
		: m_Allocator(other.m_Allocator)
	{
	}

	//Copy assignment operator
	Delegate& operator=(const Delegate& other)
	{
		Release();
		m_Allocator = other.m_Allocator;
		return *this;
	}

	//Move constructor
	Delegate(Delegate&& other) noexcept
		: m_Allocator(std::move(other.m_Allocator))
	{
	}

	//Move assignment operator
	Delegate& operator=(Delegate&& other) noexcept
	{
		Release();
		m_Allocator = std::move(other.m_Allocator);
		return *this;
	}

	//Create delegate using member function
	template<typename T, typename... Args2>
	static Delegate CreateRaw(T* pObj, RetVal(T::*pFunction)(Args..., Args2...), Args2... args)
	{
		Delegate handler;
		handler.Bind<RawDelegate<T, RetVal(Args...), Args2...>>(pObj, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using global/static function
	template<typename... Args2>
	static Delegate CreateStatic(RetVal(*pFunction)(Args..., Args2...), Args2... args)
	{
		Delegate handler;
		handler.Bind<StaticDelegate<RetVal(Args...), Args2...>>(pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using std::shared_ptr
	template<typename T, typename... Args2>
	static Delegate CreateSP(const std::shared_ptr<T>& pObject, RetVal(T::*pFunction)(Args..., Args2...), Args2... args)
	{
		Delegate handler;
		handler.Bind<SPDelegate<T, RetVal(Args...), Args2...>>(pObject, pFunction, std::forward<Args2>(args)...);
		return handler;
	}

	//Create delegate using a lambda
	template<typename TLambda, typename... Args2>
	static Delegate CreateLambda(TLambda&& lambda, Args2... args)
	{
		Delegate handler;
		handler.Bind<LambdaDelegate<TLambda, RetVal(Args...), Args2...>>(std::forward<TLambda>(lambda), std::forward<Args2>(args)...);
		return handler;
	}

	//Bind a member function
	template<typename T, typename... Args2>
	inline void BindRaw(T* pObject, RetVal(T::*pFunction)(Args..., Args2...), Args2&&... args)
	{
		*this = CreateRaw<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	//Bind a static/global function
	template<typename... Args2>
	inline void BindStatic(RetVal(*pFunction)(Args..., Args2...), Args2&&... args)
	{
		*this = CreateStatic<Args2... >(pFunction, std::forward<Args2>(args)...);
	}

	//Bind a lambda
	template<typename LambdaType, typename... Args2>
	inline void BindLambda(LambdaType&& lambda, Args2&&... args)
	{
		*this = CreateLambda<LambdaType, Args2... >(std::forward<LambdaType>(lambda), std::forward<Args2>(args)...);
	}

	//Bind a member function with a shared_ptr object
	template<typename T, typename... Args2>
	inline void BindSP(std::shared_ptr<T> pObject, RetVal(T::*pFunction)(Args..., Args2...), Args2&&... args)
	{
		*this = CreateSP<T, Args2... >(pObject, pFunction, std::forward<Args2>(args)...);
	}

	//If the allocator has a size, it means it's bound to something
	inline bool IsBound() const
	{
		return m_Allocator.HasAllocation();
	}

	//Execute the delegate with the given parameters
	RetVal Execute(Args... args) const
	{
		assert(m_Allocator.HasAllocation() && "Delegate is not bound");
		return GetDelegate()->Execute(std::forward<Args>(args)...);
	}

	RetVal ExecuteIfBound(Args... args) const
	{
		if (IsBound())
		{
			return GetDelegate()->Execute(std::forward<Args>(args)...);
		}
		return RetVal();
	}

	//Gets the owner of the deletage
	//Only valid for SPDelegate and RawDelegate.
	//Otherwise returns nullptr by default
	void* GetOwner() const
	{
		if (m_Allocator.HasAllocation())
		{
			return GetDelegate()->GetOwner();
		}
		return nullptr;
	}

	//Clear the bound delegate if it is bound to the given object.
	//Ignored when pObject is a nullptr
	inline void ClearIfBoundTo(void* pObject)
	{
		if (pObject != nullptr && IsBoundTo(pObject))
		{
			Release();
		}
	}

	//Clear the bound delegate if it exists
	inline void Clear()
	{
		Release();
	}

	inline bool IsBoundTo(void* pObject) const
	{
		if(pObject == nullptr || m_Allocator.HasAllocation() == false)
		{
			return false;
		}
		return GetDelegate()->GetOwner() == pObject;
	}

	//Determines the stack size the inline allocator can use
	//This is a public function so it can easily be requested
	constexpr static __int32 GetAllocatorStackSize()
	{
		return 32;
	}

private:
	template<typename T, typename... Args3>
	void Bind(Args3&&... args)
	{
		Release();
		void* pAlloc = m_Allocator.Allocate(sizeof(T));
		new (pAlloc) T(std::forward<Args3>(args)...);
	}

	void Release()
	{
		if (m_Allocator.HasAllocation())
		{
			GetDelegate()->~IDelegate();
			m_Allocator.Free();
		}
	}

	inline IDelegateT* GetDelegate() const
	{
		return static_cast<IDelegateT*>(m_Allocator.GetAllocation());
	}

	//Allocator for the delegate itself.
	//Delegate gets allocated inline when its is smaller or equal than 64 bytes in size.
	//Can be changed by preference
	InlineAllocator<Delegate::GetAllocatorStackSize()> m_Allocator;
};

#if CPP_DELEGATES_USE_OLD_NAMING
template<typename RetVal, typename... Args>
using SinglecastDelegate = Delegate<RetVal, Args...>;
#endif

//Delegate that can be bound to by MULTIPLE objects
template<typename... Args>
class MulticastDelegate
{
public:
	using DelegateT = Delegate<void, Args...>;

private:
	using DelegateHandlerPair = std::pair<DelegateHandle, DelegateT>;

public:
	//Default constructor
	constexpr MulticastDelegate()
		: m_Locks(0)
	{
	}

	//Default destructor
	~MulticastDelegate() noexcept = default;

	//Default copy constructor
	MulticastDelegate(const MulticastDelegate& other) = default;

	//Defaul copy assignment operator
	MulticastDelegate& operator=(const MulticastDelegate& other) = default;

	//Move constructor
	MulticastDelegate(MulticastDelegate&& other) noexcept
		: m_Events(std::move(other.m_Events)),
		m_Locks(std::move(other.m_Locks))
	{
	}

	//Move assignment operator
	MulticastDelegate& operator=(MulticastDelegate&& other) noexcept
	{
		m_Events = std::move(other.m_Events);
		m_Locks = std::move(other.m_Locks);
		return *this;
	}

	//Add delegate with the += operator
	inline DelegateHandle operator+=(DelegateT&& handler) noexcept
	{
		return Add(std::forward<DelegateT>(handler));
	}

	//Remove a delegate using its DelegateHandle
	inline bool operator-=(DelegateHandle& handle)
	{
		return Remove(handle);
	}

	inline DelegateHandle Add(DelegateT&& handler) noexcept
	{
		//Favour an empty space over a possible array reallocation
		for (size_t i = 0; i < m_Events.size(); ++i)
		{
			if (m_Events[i].first.IsValid() == false)
			{
				m_Events[i] = std::make_pair(DelegateHandle(true), std::move(handler));
				return m_Events[i].first;
			}
		}
		m_Events.push_back(std::make_pair(DelegateHandle(true), std::move(handler)));
		return m_Events.back().first;
	}

	//Bind a member function
	template<typename T, typename... Args2>
	inline DelegateHandle AddRaw(T* pObject, void(T::*pFunction)(Args..., Args2...), Args2&&... args)
	{
		return Add(DelegateT::CreateRaw(pObject, pFunction, std::forward<Args2>(args)...));
	}

	//Bind a static/global function
	template<typename... Args2>
	inline DelegateHandle AddStatic(void(*pFunction)(Args..., Args2...), Args2&&... args)
	{
		return Add(DelegateT::CreateStatic(pFunction, std::forward<Args2>(args)...));
	}

	//Bind a lambda
	template<typename LambdaType, typename... Args2>
	inline DelegateHandle AddLambda(LambdaType&& lambda, Args2&&... args)
	{
		return Add(DelegateT::CreateLambda(std::forward<LambdaType>(lambda), std::forward<Args2>(args)...));
	}

	//Bind a member function with a shared_ptr object
	template<typename T, typename... Args2>
	inline DelegateHandle AddSP(std::shared_ptr<T> pObject, void(T::*pFunction)(Args..., Args2...), Args2&&... args)
	{
		return Add(DelegateT::CreateSP(pObject, pFunction, std::forward<Args2>(args)...));
	}

	//Removes all handles that are bound from a specific object
	//Ignored when pObject is null
	//Note: Only works on Raw and SP bindings
	void RemoveObject(void* pObject)
	{
		if (pObject != nullptr)
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].second.GetOwner() == pObject)
				{
					if (IsLocked())
					{
						m_Events[i].Clear();
					}
					else
					{
						std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
						m_Events.pop_back();
					}
				}
			}
		}
	}

	//Remove a function from the event list by the handle
	bool Remove(DelegateHandle& handle)
	{
		if (handle.IsValid())
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].first == handle)
				{
					if (IsLocked())
					{
						m_Events[i].second.Clear();
					}
					else
					{
						std::swap(m_Events[i], m_Events[m_Events.size() - 1]);
						m_Events.pop_back();
					}
					handle.Reset();
					return true;
				}
			}
		}
		return false;
	}

	bool IsBoundTo(const DelegateHandle& handle) const
	{
		if (handle.IsValid())
		{
			for (size_t i = 0; i < m_Events.size(); ++i)
			{
				if (m_Events[i].first == handle)
				{
					return true;
				}
			}
		}
		return false;
	}

	//Remove all the functions bound to the delegate
	inline void RemoveAll()
	{
		if (IsLocked())
		{
			for (DelegateHandlerPair& handler : m_Events)
			{
				handler.second.Clear();
			}
		}
		else
		{
			m_Events.clear();
		}
	}

	void Compress(const size_t maxSpace = 0)
	{
		if (IsLocked() == false)
		{
			size_t toDelete = 0;
			for (size_t i = 0; i < m_Events.size() - toDelete; ++i)
			{
				if (m_Events[i].first.IsValid() == false)
				{
					std::swap(m_Events[i], m_Events[toDelete]);
					++toDelete;
				}
			}
			if (toDelete > maxSpace)
			{
				m_Events.resize(m_Events.size() - toDelete);
			}
		}
	}

	//Execute all functions that are bound
	inline void Broadcast(Args ...args)
	{
		Lock();
		for (size_t i = 0; i < m_Events.size(); ++i)
		{
			if (m_Events[i].first.IsValid())
			{
				m_Events[i].second.Execute(std::forward<Args>(args)...);
			}
		}
		Unlock();
	}

private:
	inline void Lock()
	{
		++m_Locks;
	}

	inline void Unlock()
	{
		//Unlock() should never be called more than Lock()!
		assert(m_Locks > 0);
		--m_Locks;
	}

	//Returns true is the delegate is currently broadcasting
	//If this is true, the order of the array should not be changed otherwise this causes undefined behaviour
	inline bool IsLocked() const
	{
		return m_Locks > 0;
	}

	std::vector<DelegateHandlerPair> m_Events;
	unsigned int m_Locks;
};

#endif
