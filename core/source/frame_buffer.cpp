//-----------------------------------------------------------------------------
// Include
//-----------------------------------------------------------------------------

// pch
#include "stdafx.h"

// self
#include "frame_buffer.h"

// other
#include "utils.h"

//-----------------------------------------------------------------------------
// Link-Local Functions
//-----------------------------------------------------------------------------

namespace
{
	// 範囲内で最も評価値が小さくなる要素を探す
	template<std::input_iterator Iter, class EvalFunc>
	Iter _FindMinElement(
		Iter first,
		Iter last,
		EvalFunc evalFunc
	)
	{
		// サイズゼロなら last をそのまま返す
		if (first == last)
		{
			return last;
		}
		// 評価値が最小となる要素を線形探索
		auto minIter = first;
		auto minScore = evalFunc(*first);
		for (Iter iter = std::next(first); iter != last; ++iter)
		{
			const auto score = evalFunc(*iter);
			if (score < minScore)
			{
				minIter = iter;
				minScore = score;
			}
		}
		return minIter;
	}
}

//-----------------------------------------------------------------------------
// FrameBuffer
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::FrameBuffer::FrameBuffer(double holdInSec)
: m_guard()
, m_impl()
, m_holdInSec(holdInSec)
{
	// 保持秒数は正値じゃないとダメ
	if (holdInSec <= 0.0)
	{
		throw MAKE_GENERAL_ERROR_FROM_ANY_PARAMETER("holdInSec must be semi-positive", holdInSec);
	}
}

//-----------------------------------------------------------------------------
void ayc::FrameBuffer::Clear()
{
	std::scoped_lock<std::mutex> lock(m_guard);
	m_impl.clear();
}

//-----------------------------------------------------------------------------
void ayc::FrameBuffer::PushFrame(
	const com_ptr<ID3D11Texture2D>& pTexture,
	const TimeSpan& timeSpan
)
{
	// 「現在」を確定させる
	const TimeSpan nowInTS = []() {
		return NowFromQPC();
	}();
	// バッファにフレームを追加＆バッファから賞味期限切れのフレームを削除
	/* @note:
		必ず何某かのフレームが１つは返るようにしたいので、１フレームは残す。
	*/
	{
		std::scoped_lock<std::mutex> lock(m_guard);
		m_impl.emplace_back(FRAME{ pTexture, timeSpan });
		for (;;)
		{
			if (m_impl.size() <= 1)
			{
				break;
			}
			const double frontRelativeInSec = toDurationInSec(nowInTS, m_impl.front().timeSpan);
			if (frontRelativeInSec <= m_holdInSec)
			{
				break;
			}
			m_impl.pop_front();
		}
	}
}

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::FrameBuffer::GetFrame(double relativeInSec) const
{
	// 「現在」を確定させる
	const TimeSpan nowInTS = []() {
		return NowFromQPC();
	}();
	// 相対時刻が最も近いフレームを選択する
	FRAME result = {};
	{
		std::scoped_lock<std::mutex> lock(m_guard);
		if (m_impl.empty())
		{
			throw MAKE_GENERAL_ERROR("FrameBuffer is Empty");
		}
		result = *_FindMinElement(
			m_impl.cbegin(),
			m_impl.cend(),
			[&](const FRAME& f)
			{
				return std::abs(toDurationInSec(nowInTS, f.timeSpan) - relativeInSec);
			}
		);
	}
	return result.pTexture;
}


//-----------------------------------------------------------------------------
// FreezedFrameBuffer
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer::FreezedFrameBuffer()
: m_impl()
{
	// nop
}

//-----------------------------------------------------------------------------
ayc::FreezedFrameBuffer::FreezedFrameBuffer(
	const FrameBuffer& frameBuffer,
	double durationInSec
)
: m_impl()
{
	// 「現在」を確定させる
	const TimeSpan nowInTS = []()
	{
		return NowFromQPC();
	}();
	// スナップショット時間長を解決
	const auto actualDuration = std::min(
		durationInSec,
		frameBuffer.m_holdInSec
	);
	// 範囲内のフレームを抽出
	/* @note:
		FrameBuffer の挙動（必ず１枚は有効なフレームが存在する）と揃えたいので、
		ここでも最低でも１フレームは返す。
	*/
	{
		std::scoped_lock<std::mutex> lock(frameBuffer.m_guard);
		auto& srcImpl = frameBuffer.m_impl;
		m_impl.reserve(srcImpl.size());
		for (const auto& frame : srcImpl)
		{
			const auto relativeInSec = toDurationInSec(nowInTS, frame.timeSpan);
			if (relativeInSec > actualDuration)
			{
				continue;
			}
			m_impl.emplace_back(FRAME{ frame.pTexture, relativeInSec });
		}
		if (m_impl.empty() and !srcImpl.empty())
		{
			const auto frame = srcImpl.back();
			const auto relativeInSec = toDurationInSec(nowInTS, frame.timeSpan);
			m_impl.emplace_back(FRAME{ frame.pTexture, relativeInSec });
		}
	}
	// フレームを時刻で降順にソート
	{
		std::sort(
			m_impl.begin(),
			m_impl.end(),
			[](const FRAME& lho, const FRAME& rho) {return lho.relativeInSec > rho.relativeInSec; }
		);
	}
}

// 指定相対時刻と最も近いフレームのインデックスを取得する
std::size_t ayc::FreezedFrameBuffer::GetFrameIndex(double relativeInSec) const
{
	auto iter = _FindMinElement(
		m_impl.cbegin(),
		m_impl.cend(),
		[&](const FRAME& f)
		{
			return std::abs(relativeInSec - f.relativeInSec);
		}
	);
	return iter - m_impl.cbegin();
}

//-----------------------------------------------------------------------------
ayc::com_ptr<ID3D11Texture2D> ayc::FreezedFrameBuffer::operator [](std::size_t index) const
{
	return m_impl[index].pTexture;
}
